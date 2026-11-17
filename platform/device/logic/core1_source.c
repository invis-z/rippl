#include "device/core1_stream.h"

#include <stddef.h>

#include "device/core1_control.h"

#include "core1_ring.h"
#include "core1_source_shared.h"

enum {
    LOW_RING_PARTS = 4,
    LOW_RING_BOOST = 4,
    READ_SECTOR_BYTES = 512
};

void core1_source_init(Core1Source *source, StreamRegion region, FlacFileOpener files,
                       FlacDecoder *decoder, uint32_t fill_budget)
{
    *source = (Core1Source){
        .region = region, .ring_capacity = region.ring_bytes, .fill_budget = fill_budget};
    flac_source_init(&source->flac, files, decoder);
}

void core1_source_set_wake(Core1Source *source, void (*wake)(void))
{
    source->wake = wake;
}

static void wake_core1(const Core1Source *source)
{
    if (source->wake != NULL) {
        source->wake();
    }
}

void core1_source_use_ring(Core1Source *source, uint32_t bytes)
{
    source->region.ring_bytes = bytes;
    source->region.control->ring_bytes = bytes;
}

static void drop_pending_read(Core1Source *source)
{
    while (source->async_pending &&
           source->async.poll(source->async.context) == CORE1_READ_PENDING) {
    }
    source->async_pending = false;
}

void core1_source_stop(Core1Source *source)
{
    drop_pending_read(source);
    if (source->streaming) {
        source->region.control->generation = 0;
        source->streaming = false;
        wake_core1(source);
    }
    flac_source_close(&source->flac);
    if (source->file_open) {
        source->flac.files.close(source->flac.files.context);
        source->file_open = false;
    }
}

static uint32_t next_generation(Core1Source *source)
{
    source->generation++;
    if (source->generation == 0) {
        source->generation = 1;
    }
    return source->generation;
}

void core1_source_reset_equalizer(Core1Source *source)
{
    source->reset_equalizer = true;
}

void core1_source_set_word_output(Core1Source *source, bool words)
{
    source->word_output = words;
}

void core1_source_set_async(Core1Source *source, Core1AsyncRead async)
{
    drop_pending_read(source);
    source->async = async;
}

static void publish_written(Core1Source *source, uint32_t length)
{
    source->written += length;
    publish();
    source->region.control->written = source->written;
    wake_core1(source);
}

static bool finish_pending_read(Core1Source *source)
{
    if (!source->async_pending) {
        return true;
    }
    Core1ReadState state = source->async.poll(source->async.context);
    if (state == CORE1_READ_PENDING) {
        return false;
    }
    source->async_pending = false;
    if (state == CORE1_READ_FAILED &&
        !source->input.read(source->input.context, source->async_offset, source->async_destination,
                            source->async_length)) {
        source->read_failed = true;
        return false;
    }
    publish_written(source, source->async_length);
    return true;
}

static bool start_async_read(Core1Source *source, void *destination, uint32_t length)
{
    if (source->async.start == NULL) {
        return false;
    }
    uint32_t started =
        source->async.start(source->async.context, source->written, destination, length);
    if (started == 0 || started > length) {
        return false;
    }
    source->async_pending = true;
    source->async_offset = source->written;
    source->async_length = started;
    source->async_destination = destination;
    return true;
}

void core1_source_begin(Core1Source *source, AudioTrackInfo *track)
{
    volatile StreamControl *control = source->region.control;
    source->factor = upsampler_factor(control->sample_rate);
    track->play_rate = control->sample_rate * source->factor;
    control->equalizer_reset = source->reset_equalizer ? 1u : 0u;
    control->word_output = source->word_output ? 1u : 0u;
    source->reset_equalizer = false;
    publish();
    control->generation = next_generation(source);
    source->streaming = true;
    wake_core1(source);
}

static bool open_stream(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    if (!flac_source_open_at(&source->flac, path, start_ms, track)) {
        return false;
    }
    const FlacInfo *info = &track->stream;
    FlacDecoder *decoder = source->flac.decoder;
    source->input = decoder->bits.input;
    source->start_offset = flac_position(decoder);
    source->end_offset = source->input.size;
    source->written = source->start_offset;
    source->lead_trim = 0;
    source->play_from = 0;
    source->play_end = UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = info->bits_per_sample;
    source->channels = info->channels;
    volatile StreamControl *control = source->region.control;
    control->sample_rate = info->sample_rate;
    control->channels = info->channels;
    control->bits_per_sample = info->bits_per_sample;
    control->min_block = info->min_block;
    control->max_block = info->max_block;
    control->total_samples = info->total_samples;
    control->first_sample = 0;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    control->codec = STREAM_CODEC_NATIVE;
    core1_source_use_ring(source, source->ring_capacity);
    core1_source_begin(source, track);
    return true;
}

void core1_source_fill(Core1Source *source)
{
    if (!source->streaming || source->read_failed || !finish_pending_read(source)) {
        return;
    }
    volatile StreamControl *control = source->region.control;
    uint32_t released =
        control->acked_generation == source->generation ? control->released : source->start_offset;
    observe();
    uint32_t target = smaller(released + source->region.ring_bytes, source->end_offset);
    uint32_t on_sector = target - target % READ_SECTOR_BYTES;
    if (target < source->end_offset && on_sector > source->written) {
        target = on_sector;
    }
    uint32_t budget = source->fill_budget;
    if (source->written - released < source->region.ring_bytes / LOW_RING_PARTS) {
        budget *= LOW_RING_BOOST;
    }
    while (source->written < target && budget > 0) {
        uint32_t position = source->written % source->region.ring_bytes;
        uint32_t length = smaller(smaller(target - source->written, budget),
                                  source->region.ring_bytes - position);
        uint32_t past_sector = (source->written + length) % READ_SECTOR_BYTES;
        if (past_sector != 0 && source->written + length < target) {
            if (length > past_sector) {
                length -= past_sector;
            } else if (source->written % READ_SECTOR_BYTES == 0) {
                break;
            }
        }
        void *destination = (void *)(uintptr_t)&source->region.ring[position];
        if (start_async_read(source, destination, length)) {
            return;
        }
        if (!source->input.read(source->input.context, source->written, destination, length)) {
            source->read_failed = true;
            return;
        }
        budget -= length;
        publish_written(source, length);
    }
}

static void drop_stale_slots(Core1Source *source)
{
    volatile StreamControl *control = source->region.control;
    for (uint32_t index = 0; index < STREAM_SLOTS; index++) {
        volatile StreamSlot *slot = &control->slots[index];
        if (slot->state == STREAM_SLOT_FULL && slot->generation != source->generation) {
            slot->state = STREAM_SLOT_EMPTY;
            wake_core1(source);
        }
    }
}

static void advance_slot(Core1Source *source)
{
    source->region.control->slots[source->slot].state = STREAM_SLOT_EMPTY;
    source->slot = (source->slot + 1) % STREAM_SLOTS;
    wake_core1(source);
}

static int32_t *source_row(const Core1Source *source, uint32_t samples, uint32_t channel)
{
    volatile StreamControl *control = source->region.control;
    uint32_t offset = control->slot_offset;
    uint32_t stride = control->slot_samples;
    if (stride == 0 || samples > stride || offset > STREAM_SAMPLE_WORDS ||
        stride > (STREAM_SAMPLE_WORDS - offset) / (STREAM_SLOTS * FLAC_MAX_CHANNELS)) {
        return NULL;
    }
    return slot_row(source->region.samples, offset, stride, source->slot, channel);
}

static uint32_t scaled(const Core1Source *source, uint32_t samples)
{
    uint32_t factor = source->factor > 0 ? source->factor : 1;
    return samples == UINT32_MAX ? UINT32_MAX : samples * factor;
}

static AudioFrameResult next_frame(void *context, AudioFrame *frame)
{
    Core1Source *source = context;
    core1_source_fill(source);
    if (!source->streaming || source->read_failed) {
        return AUDIO_FRAME_ERROR;
    }
    for (;;) {
        drop_stale_slots(source);
        volatile StreamSlot *slot = &source->region.control->slots[source->slot];
        if (slot->state != STREAM_SLOT_FULL) {
            return AUDIO_FRAME_WAIT;
        }
        observe();
        if (slot->result != AUDIO_FRAME_READY) {
            return slot->result == AUDIO_FRAME_END ? AUDIO_FRAME_END : AUDIO_FRAME_ERROR;
        }
        uint32_t first = slot->first_sample;
        uint32_t play_end = scaled(source, source->play_end);
        uint32_t lead_trim = scaled(source, source->lead_trim);
        uint32_t play_from = scaled(source, source->play_from);
        if (first >= play_end) {
            return AUDIO_FRAME_END;
        }
        uint32_t end = smaller(first + slot->samples, play_end);
        uint32_t from = first > lead_trim ? first : lead_trim;
        from = from > play_from ? from : play_from;
        if (from >= end) {
            advance_slot(source);
            continue;
        }
        uint32_t skip = from - first;
        if (slot->words != 0) {
            const uint32_t *words = (const uint32_t *)source_row(source, slot->samples, 0);
            if (words == NULL) {
                return AUDIO_FRAME_ERROR;
            }
            *frame = (AudioFrame){
                .words = words + WORD_CHANNELS * skip,
                .first_sample = from - lead_trim,
                .samples = end - from,
                .bits = slot->bits != 0 ? (uint8_t)slot->bits : source->bits_per_sample,
                .equalized = slot->equalized != 0,
            };
            return AUDIO_FRAME_READY;
        }
        int32_t *left = source_row(source, slot->samples, 0);
        int32_t *right = source->channels > 1 ? source_row(source, slot->samples, 1) : left;
        if (left == NULL || right == NULL) {
            return AUDIO_FRAME_ERROR;
        }
        *frame = (AudioFrame){
            .left = left + skip,
            .right = right + skip,
            .first_sample = from - lead_trim,
            .samples = end - from,
            .bits = slot->bits != 0 ? (uint8_t)slot->bits : source->bits_per_sample,
            .equalized = slot->equalized != 0,
        };
        return AUDIO_FRAME_READY;
    }
}

static void release_frame(void *context)
{
    advance_slot(context);
}

static void close_stream(void *context)
{
    core1_source_stop(context);
}

AudioSource core1_source_interface(Core1Source *source)
{
    return (AudioSource){open_stream, next_frame, release_frame, close_stream, source};
}

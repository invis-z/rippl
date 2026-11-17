#include "device/core1_stream.h"

#include <stddef.h>

#include "device/core1_control.h"

#include "core1_ring.h"

enum {
    EQUALIZED_BITS = 24,
    UPSAMPLE_CHUNK = 32,
    WORD_SHIFT = 8,
    SAMPLE_MAX = 8388607,
    SAMPLE_MIN = -8388608,
    WORD_BYTES = 4
};

static void use_banks(Core1Flac *flac, uint32_t max_block)
{
    uint32_t half = FLAC_MAX_BLOCK / 2;
    if (max_block > 0 && max_block <= half) {
        flac_set_buffers(flac->decoder, flac->banks.second, flac->banks.second + half, half);
    } else {
        flac_set_buffers(flac->decoder, flac->banks.first, flac->banks.second, FLAC_MAX_BLOCK);
    }
}

static void start_flac(void *context, FlacInput input, FlacInfo info, uint32_t first_sample,
                       const bool *starved)
{
    Core1Flac *flac = context;
    (void)first_sample;
    flac->starved = starved;
    use_banks(flac, info.max_block);
    flac_start(flac->decoder, input, info);
}

static Core1DecodeResult decode_flac(void *context, Core1Block *block)
{
    Core1Flac *flac = context;
    FlacDecoder *decoder = flac->decoder;
    uint32_t frame_start = flac_position(decoder);
    FlacResult result = flac_decode_frame(decoder);
    if (*flac->starved) {
        flac_rewind(decoder, frame_start);
        return CORE1_STARVED;
    }
    if (result == FLAC_END) {
        return CORE1_FINISHED;
    }
    if (result != FLAC_OK) {
        return CORE1_FAILED;
    }
    for (int channel = 0; channel < FLAC_MAX_CHANNELS; channel++) {
        block->channels[channel] =
            flac_channel(decoder, channel < decoder->info.channels ? channel : 0);
    }
    block->first_sample = decoder->frame_first_sample;
    block->samples = decoder->block_samples;
    block->position = flac_position(decoder);
    block->join = decoder->join;
    return CORE1_DECODED;
}

Core1Decoder core1_flac_decoder(Core1Flac *flac, FlacDecoder *decoder, Core1FlacBanks banks)
{
    *flac = (Core1Flac){.decoder = decoder, .banks = banks, .starved = NULL};
    flac_defer_join(decoder, true);
    return (Core1Decoder){start_flac, decode_flac, flac};
}

void core1_stream_init(Core1Stream *stream, StreamRegion region, Core1Decoder decoder)
{
    *stream = (Core1Stream){.region = region,
                            .ring_capacity = region.ring_bytes,
                            .decoder = decoder,
                            .slot_samples = STREAM_SLOT_SAMPLES};
    stream->decoders[STREAM_CODEC_NATIVE] = decoder;
}

void core1_stream_set_slots(Core1Stream *stream, uint32_t offset, uint32_t samples)
{
    stream->slot_offset = offset;
    stream->slot_samples = samples;
}

static int32_t *stream_row(const Core1Stream *stream, uint32_t slot, uint32_t channel)
{
    return slot_row(stream->region.samples, stream->slot_offset, stream->slot_samples, slot,
                    channel);
}

void core1_stream_set_clock(Core1Stream *stream, uint32_t (*ticks)(void), uint32_t mask,
                            uint32_t window)
{
    stream->ticks = ticks;
    stream->tick_mask = mask;
    stream->load_window = window;
    stream->last_tick = ticks();
    stream->busy_ticks = 0;
    stream->wall_ticks = 0;
}

static uint32_t tick_now(const Core1Stream *stream)
{
    return stream->ticks != NULL ? stream->ticks() : 0;
}

static void count_wall(Core1Stream *stream)
{
    if (stream->ticks == NULL) {
        return;
    }
    uint32_t now = stream->ticks();
    stream->wall_ticks += (now - stream->last_tick) & stream->tick_mask;
    stream->last_tick = now;
    if (stream->wall_ticks >= stream->load_window) {
        uint64_t share = (uint64_t)stream->busy_ticks * 100u / stream->wall_ticks;
        stream->region.control->load_percent = (uint32_t)share;
        stream->busy_ticks = 0;
        stream->wall_ticks = 0;
    }
}

void core1_stream_set_equalizer(Core1Stream *stream, Equalizer *equalizer,
                                volatile Core1EqualizerShare *share)
{
    equalizer_init(equalizer);
    stream->equalizer = equalizer;
    stream->share = share;
    stream->share_version = 0;
}

static bool fit_for(const volatile EqualizerFit *shared, uint32_t rate, EqualizerFit *fit)
{
    *fit = *(const EqualizerFit *)(uintptr_t)shared;
    return fit->stage_count > 0 && fit->rate == rate;
}

static void follow_share(Core1Stream *stream)
{
    uint32_t version = stream->share->version;
    if (version == stream->share_version && !stream->replace_equalizer) {
        return;
    }
    observe();
    stream->share_version = version;
    stream->replace_equalizer = false;
    EqualizerCurve curve = stream->share->curve;
    uint32_t rate = stream->region.control->sample_rate;
    uint32_t factor = stream->upsampler.factor;
    EqualizerFit fit;
    bool first =
        upsampler_active(&stream->upsampler) && fit_for(&stream->share->before_fit, rate, &fit);
    bool fitted = first || fit_for(&stream->share->after_fit, rate * factor, &fit);
    stream->equalize_first = first;
    equalizer_set_fit(stream->equalizer, NULL);
    equalizer_set_rate(stream->equalizer, first ? rate : rate * factor);
    equalizer_set_curve(stream->equalizer, &curve);
    equalizer_set_fit(stream->equalizer, fitted ? &fit : NULL);
}

static void widen(int32_t *samples, uint32_t count, uint32_t shift)
{
    for (uint32_t index = 0; index < count; index++) {
        samples[index] = samples[index] * (1 << shift);
    }
}

static uint32_t equalize_slot(Core1Stream *stream, uint32_t slot, uint32_t samples, uint32_t bits)
{
    if (stream->equalizer == NULL || stream->share->enabled == 0) {
        return 0;
    }
    follow_share(stream);
    if (!equalizer_active(stream->equalizer) || bits == 0 || bits > EQUALIZED_BITS) {
        return 0;
    }
    int32_t *left = stream_row(stream, slot, 0);
    int32_t *right = stream->channels > 1 ? stream_row(stream, slot, 1) : NULL;
    widen(left, samples, EQUALIZED_BITS - bits);
    if (right != NULL) {
        widen(right, samples, EQUALIZED_BITS - bits);
    }
    equalizer_process_block(stream->equalizer, (EqualizerBlock){left, right, samples, 1});
    return 1;
}

void core1_stream_set_decoder(Core1Stream *stream, uint32_t codec, Core1Decoder decoder)
{
    if (codec < STREAM_CODECS) {
        stream->decoders[codec] = decoder;
    }
}

static void copy_run(uint8_t *out, const volatile uint8_t *in, uint32_t length)
{
    uint32_t index = 0;
    for (; index < length && ((uintptr_t)(in + index) & (WORD_BYTES - 1u)) != 0; index++) {
        out[index] = in[index];
    }
    const volatile uint32_t *words = (const volatile uint32_t *)(uintptr_t)(in + index);
    for (; index + WORD_BYTES <= length; index += WORD_BYTES) {
        uint32_t word = *words++;
        __builtin_memcpy(&out[index], &word, sizeof word);
    }
    for (; index < length; index++) {
        out[index] = in[index];
    }
}

static void copy_from_ring(const StreamRegion *region, uint32_t position, uint8_t *out,
                           uint32_t length)
{
    uint32_t first = position % region->ring_bytes;
    uint32_t part = smaller(length, region->ring_bytes - first);
    copy_run(out, &region->ring[first], part);
    copy_run(out + part, region->ring, length - part);
}

static bool read_ring(void *context, uint32_t offset, void *buffer, uint32_t length)
{
    Core1Stream *stream = context;
    volatile StreamControl *control = stream->region.control;
    if (control->generation != stream->generation) {
        return false;
    }
    uint32_t position = stream->start_offset + offset;
    if (position + length > control->written) {
        stream->starved = true;
        return false;
    }
    observe();
    copy_from_ring(&stream->region, position, buffer, length);
    return true;
}

static void adopt(Core1Stream *stream, uint32_t generation)
{
    volatile StreamControl *control = stream->region.control;
    observe();
    FlacInfo info = {
        .sample_rate = control->sample_rate,
        .channels = (uint8_t)control->channels,
        .bits_per_sample = (uint8_t)control->bits_per_sample,
        .min_block = (uint16_t)control->min_block,
        .max_block = (uint16_t)control->max_block,
        .total_samples = control->total_samples,
    };
    uint32_t ring = control->ring_bytes;
    stream->region.ring_bytes =
        ring > 0 && ring <= stream->ring_capacity ? ring : stream->ring_capacity;
    stream->generation = generation;
    stream->start_offset = control->start_offset;
    stream->end_offset = control->end_offset;
    stream->channels =
        control->channels < FLAC_MAX_CHANNELS ? control->channels : FLAC_MAX_CHANNELS;
    stream->slot = 0;
    stream->pending = (Core1Block){0};
    stream->emitted = 0;
    stream->handing = false;
    stream->holding = false;
    stream->finished = false;
    stream->starved = false;
    stream->words = control->word_output != 0;
    upsampler_start(&stream->upsampler, info.sample_rate,
                    info.bits_per_sample <= EQUALIZED_BITS ? EQUALIZED_BITS : info.bits_per_sample);
    stream->upsampled_timed = false;
    stream->flush_pending = upsampler_active(&stream->upsampler);
    control->slot_offset = stream->slot_offset;
    control->slot_samples = stream->slot_samples;
    if (stream->equalizer != NULL) {
        stream->replace_equalizer = true;
        stream->equalize_first = false;
        equalizer_set_rate(stream->equalizer, info.sample_rate * stream->upsampler.factor);
        if (control->equalizer_reset != 0) {
            control->equalizer_reset = 0;
            equalizer_reset(stream->equalizer);
        }
    }
    FlacInput input = {
        .read = read_ring, .context = stream, .size = stream->end_offset - stream->start_offset};
    uint32_t codec = control->codec;
    stream->decoder = codec < STREAM_CODECS ? stream->decoders[codec] : (Core1Decoder){0};
    if (stream->decoder.start != NULL) {
        stream->decoder.start(stream->decoder.context, input, info, control->first_sample,
                              &stream->starved);
    }
    control->released = stream->start_offset;
    publish();
    control->acked_generation = generation;
}

typedef struct ChannelOut {
    int32_t *left;
    int32_t *right;
    uint32_t step;
    uint32_t shift;
} ChannelOut;

static void read_channels(const Core1Stream *stream, uint32_t from, uint32_t count, ChannelOut out,
                          bool silence)
{
    const Core1Block *block = &stream->pending;
    uint32_t stride = block->stride > 0 ? block->stride : 1;
    bool stereo = stream->channels == FLAC_MAX_CHANNELS && out.right != NULL;
    FlacJoin join = (FlacJoin)block->join;
    if (!silence && stereo && out.shift == 0 && stride == 1 && block->narrow[0] == NULL &&
        block->narrow[1] == NULL) {
        const int32_t *first = block->channels[0] + from;
        const int32_t *second = block->channels[1] + from;
        if (out.step == 1) {
            flac_join(join, first, second, out.left, out.right, count);
            return;
        }
        if (out.step == WORD_CHANNELS && out.right == out.left + 1) {
            flac_join_interleaved(join, first, second, out.left, count);
            return;
        }
    }
    int32_t scale = (int32_t)(1u << out.shift);
    for (uint32_t index = 0; index < count; index++) {
        uint32_t at = (from + index) * stride;
        int32_t pair[FLAC_MAX_CHANNELS] = {0, 0};
        for (uint32_t channel = 0; !silence && channel < stream->channels; channel++) {
            const int16_t *narrow = block->narrow[channel];
            pair[channel] = narrow != NULL ? narrow[at] : block->channels[channel][at];
        }
        if (!silence && stereo && join != FLAC_JOIN_NONE) {
            flac_join_pair(join, pair[0], pair[1], &pair[0], &pair[1]);
        }
        out.left[index * out.step] = pair[0] * scale;
        if (out.right != NULL) {
            out.right[index * out.step] = pair[1] * scale;
        }
    }
}

static void copy_channels(Core1Stream *stream, uint32_t slot, uint32_t samples)
{
    int32_t *right = stream->channels > 1 ? stream_row(stream, slot, 1) : NULL;
    read_channels(stream, stream->emitted, samples,
                  (ChannelOut){stream_row(stream, slot, 0), right, 1, 0}, false);
}

static uint32_t source_bits(const Core1Stream *stream);

static void interleave_channels(Core1Stream *stream, int32_t *frames, uint32_t samples)
{
    int32_t *right = stream->channels > 1 ? frames + 1 : NULL;
    read_channels(stream, stream->emitted, samples, (ChannelOut){frames, right, WORD_CHANNELS, 0},
                  false);
}

static uint32_t equalize_frames(Core1Stream *stream, int32_t *frames, uint32_t samples,
                                uint32_t bits)
{
    if (stream->equalizer == NULL || stream->share->enabled == 0) {
        return 0;
    }
    follow_share(stream);
    if (!equalizer_active(stream->equalizer) || bits == 0 || bits > EQUALIZED_BITS) {
        return 0;
    }
    widen(frames, WORD_CHANNELS * samples, EQUALIZED_BITS - bits);
    int32_t *right = stream->channels > 1 ? frames + 1 : NULL;
    equalizer_process_block(stream->equalizer,
                            (EqualizerBlock){frames, right, samples, WORD_CHANNELS});
    return 1;
}

static int32_t clamped_sample(int32_t sample)
{
    return sample > SAMPLE_MAX ? SAMPLE_MAX : sample < SAMPLE_MIN ? SAMPLE_MIN : sample;
}

static void to_words(int32_t *frames, uint32_t samples, uint32_t bits, uint32_t channels)
{
    uint32_t count = WORD_CHANNELS * samples;
    if (bits > EQUALIZED_BITS) {
        for (uint32_t index = 0; channels == 1 && index < count; index += WORD_CHANNELS) {
            frames[index + 1] = frames[index];
        }
        return;
    }
    int32_t scale = (int32_t)(1u << (EQUALIZED_BITS - bits));
    for (uint32_t index = 0; index < count; index += WORD_CHANNELS) {
        uint32_t left = (uint32_t)clamped_sample(frames[index] * scale) << WORD_SHIFT;
        uint32_t right = channels == 1
                             ? left
                             : (uint32_t)clamped_sample(frames[index + 1] * scale) << WORD_SHIFT;
        frames[index] = (int32_t)left;
        frames[index + 1] = (int32_t)right;
    }
}

static void emit_words(Core1Stream *stream, volatile StreamSlot *slot, uint32_t samples)
{
    int32_t *frames = stream_row(stream, stream->slot, 0);
    interleave_channels(stream, frames, samples);
    uint32_t bits = source_bits(stream);
    uint32_t equalized = equalize_frames(stream, frames, samples, bits);
    to_words(frames, samples, equalized != 0 ? EQUALIZED_BITS : bits, stream->channels);
    slot->equalized = equalized;
    slot->bits = equalized != 0 ? EQUALIZED_BITS : 0;
    slot->words = 1;
}

static void hand_over(Core1Stream *stream, uint32_t outcome)
{
    volatile StreamSlot *slot = &stream->region.control->slots[stream->slot];
    slot->generation = stream->generation;
    slot->result = outcome;
    publish();
    slot->state = STREAM_SLOT_FULL;
    stream->slot = (stream->slot + 1) % STREAM_SLOTS;
}

static uint32_t source_bits(const Core1Stream *stream)
{
    return stream->region.control->bits_per_sample;
}

static uint32_t upsampled_bits(const Core1Stream *stream)
{
    uint32_t bits = source_bits(stream);
    return bits <= EQUALIZED_BITS ? EQUALIZED_BITS : bits;
}

static bool equalizes_first(Core1Stream *stream)
{
    if (stream->equalizer == NULL || stream->share->enabled == 0) {
        return false;
    }
    follow_share(stream);
    uint32_t bits = source_bits(stream);
    return stream->equalize_first && equalizer_active(stream->equalizer) && bits != 0 &&
           bits <= EQUALIZED_BITS;
}

static void gather(Core1Stream *stream, uint32_t from, uint32_t count, bool silence,
                   int32_t (*values)[UPSAMPLE_CHUNK])
{
    uint32_t widen = upsampled_bits(stream) - source_bits(stream);
    int32_t *right = stream->channels > 1 ? values[1] : NULL;
    read_channels(stream, from, count, (ChannelOut){values[0], right, 1, widen}, silence);
}

static uint32_t render_rows(Core1Stream *stream, uint32_t slot, uint32_t produced)
{
    int32_t *right = stream->channels > 1 ? stream_row(stream, slot, 1) + produced : NULL;
    return upsampler_render(&stream->upsampler, stream_row(stream, slot, 0) + produced, right);
}

static uint32_t render_frames(Core1Stream *stream, uint32_t slot, uint32_t produced)
{
    int32_t left[UPSAMPLER_MAX_FACTOR];
    int32_t right[UPSAMPLER_MAX_FACTOR];
    bool stereo = stream->channels > 1;
    uint32_t made = upsampler_render(&stream->upsampler, left, stereo ? right : NULL);
    int32_t *frames = stream_row(stream, slot, 0) + WORD_CHANNELS * produced;
    for (uint32_t index = 0; index < made; index++) {
        frames[WORD_CHANNELS * index] = left[index];
        frames[WORD_CHANNELS * index + 1] = stereo ? right[index] : left[index];
    }
    return made;
}

static uint32_t upsample(Core1Stream *stream, uint32_t slot, uint32_t count, bool silence)
{
    bool first = equalizes_first(stream);
    stream->equalized_first = first;
    uint32_t produced = 0;
    int32_t values[FLAC_MAX_CHANNELS][UPSAMPLE_CHUNK];
    for (uint32_t done = 0; done < count; done += UPSAMPLE_CHUNK) {
        uint32_t chunk = smaller(count - done, UPSAMPLE_CHUNK);
        gather(stream, stream->emitted + done, chunk, silence, values);
        if (first) {
            equalizer_process_block(
                stream->equalizer,
                (EqualizerBlock){values[0], stream->channels > 1 ? values[1] : NULL, chunk, 1});
        }
        for (uint32_t index = 0; index < chunk; index++) {
            for (uint32_t channel = 0; channel < stream->channels; channel++) {
                upsampler_put(&stream->upsampler, channel, values[channel][index]);
            }
            produced += stream->words ? render_frames(stream, slot, produced)
                                      : render_rows(stream, slot, produced);
            upsampler_advance(&stream->upsampler);
        }
    }
    return produced;
}

static void hand_over_upsampled(Core1Stream *stream, uint32_t produced)
{
    volatile StreamSlot *slot = &stream->region.control->slots[stream->slot];
    uint32_t factor = stream->upsampler.factor;
    uint32_t bits = upsampled_bits(stream);
    if (stream->words) {
        int32_t *frames = stream_row(stream, stream->slot, 0);
        slot->equalized =
            stream->equalized_first ? 1u : equalize_frames(stream, frames, produced, bits);
        to_words(frames, produced, bits, stream->channels);
    } else {
        slot->equalized =
            stream->equalized_first ? 1u : equalize_slot(stream, stream->slot, produced, bits);
    }
    slot->bits = bits;
    slot->words = stream->words ? 1u : 0u;
    slot->first_sample = stream->upsampled_time * factor;
    slot->samples = produced;
    stream->upsampled_time += produced / factor;
    hand_over(stream, AUDIO_FRAME_READY);
}

static void emit_upsampled(Core1Stream *stream)
{
    if (!stream->upsampled_timed) {
        stream->upsampled_time = stream->pending.first_sample;
        stream->upsampled_timed = true;
    }
    uint32_t count = smaller(stream->pending.samples - stream->emitted,
                             stream->slot_samples / stream->upsampler.factor);
    uint32_t produced = upsample(stream, stream->slot, count, false);
    stream->emitted += count;
    stream->handing = stream->emitted < stream->pending.samples;
    hand_over_upsampled(stream, produced);
}

static void emit_flush(Core1Stream *stream)
{
    stream->flush_pending = false;
    uint32_t produced = upsample(stream, stream->slot, UPSAMPLER_DELAY, true);
    hand_over_upsampled(stream, produced);
}

static void emit_piece(Core1Stream *stream)
{
    if (upsampler_active(&stream->upsampler)) {
        emit_upsampled(stream);
        return;
    }
    volatile StreamSlot *slot = &stream->region.control->slots[stream->slot];
    uint32_t samples = smaller(stream->pending.samples - stream->emitted, stream->slot_samples);
    if (stream->words) {
        emit_words(stream, slot, samples);
    } else {
        copy_channels(stream, stream->slot, samples);
        slot->equalized = equalize_slot(stream, stream->slot, samples, source_bits(stream));
        slot->bits = slot->equalized != 0 ? EQUALIZED_BITS : 0;
        slot->words = 0;
    }
    slot->first_sample = stream->pending.first_sample + stream->emitted;
    slot->samples = samples;
    stream->emitted += samples;
    stream->handing = stream->emitted < stream->pending.samples;
    hand_over(stream, AUDIO_FRAME_READY);
}

static void end_stream(Core1Stream *stream, Core1DecodeResult result)
{
    volatile StreamSlot *slot = &stream->region.control->slots[stream->slot];
    slot->first_sample = 0;
    slot->samples = 0;
    slot->equalized = 0;
    slot->bits = 0;
    slot->words = 0;
    stream->finished = true;
    hand_over(stream, result == CORE1_FINISHED ? AUDIO_FRAME_END : AUDIO_FRAME_ERROR);
}

static void take_result(Core1Stream *stream, Core1DecodeResult result, const Core1Block *block)
{
    if (result == CORE1_DECODED) {
        stream->pending = *block;
        stream->emitted = 0;
        stream->handing = true;
        stream->region.control->released = stream->start_offset + block->position;
        return;
    }
    stream->held = result;
    stream->holding = true;
}

static bool slot_empty(const Core1Stream *stream)
{
    return stream->region.control->slots[stream->slot].state == STREAM_SLOT_EMPTY;
}

static bool hand_over_when_free(Core1Stream *stream)
{
    if (!slot_empty(stream)) {
        return false;
    }
    uint32_t began = tick_now(stream);
    if (stream->handing) {
        emit_piece(stream);
    } else if (stream->held == CORE1_FINISHED && stream->flush_pending && stream->upsampled_timed) {
        emit_flush(stream);
    } else {
        stream->holding = false;
        end_stream(stream, stream->held);
    }
    stream->busy_ticks += (tick_now(stream) - began) & stream->tick_mask;
    return true;
}

bool core1_stream_step(Core1Stream *stream)
{
    count_wall(stream);
    volatile StreamControl *control = stream->region.control;
    uint32_t generation = control->generation;
    if (generation != stream->generation) {
        if (generation == 0) {
            stream->generation = 0;
            return false;
        }
        adopt(stream, generation);
        return true;
    }
    if (generation == 0 || stream->finished) {
        return false;
    }
    if (stream->handing || stream->holding) {
        return hand_over_when_free(stream);
    }
    stream->starved = false;
    Core1Block block = {0};
    if (stream->decoder.decode == NULL) {
        take_result(stream, CORE1_FAILED, &block);
        (void)hand_over_when_free(stream);
        return true;
    }
    uint32_t started = tick_now(stream);
    Core1DecodeResult result = stream->decoder.decode(stream->decoder.context, &block);
    stream->busy_ticks += (tick_now(stream) - started) & stream->tick_mask;
    if (control->generation != stream->generation) {
        return true;
    }
    if (result == CORE1_STARVED) {
        stream->starved = false;
        return false;
    }
    take_result(stream, result, &block);
    (void)hand_over_when_free(stream);
    return true;
}

void core1_stream_set_wait(Core1Stream *stream, void (*wait)(void))
{
    stream->wait = wait;
}

void core1_stream_serve(Core1Stream *stream)
{
    if (!core1_stream_step(stream) && stream->wait != NULL &&
        stream->region.control->sleep_allowed != 0) {
        stream->wait();
    }
}

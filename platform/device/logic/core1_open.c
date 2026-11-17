#include "device/core1_stream.h"

#include <stddef.h>

#include "codec/ogg.h"
#include "codec/qoa.h"
#include "device/core1_control.h"
#include "device/mp3_probe.h"
#include "device/wav_format.h"

#include "core1_ring.h"
#include "core1_source_shared.h"

enum {
    MP3_OUTPUT_BITS = 24,
    OPUS_OUTPUT_BITS = 16,
    OPUS_FRAME_SAMPLES = 960,
    MILLISECONDS_PER_SECOND = 1000
};

static AudioSource opened_by(Core1Source *source,
                             bool (*open)(void *context, const char *path, uint32_t start_ms,
                                          AudioTrackInfo *track))
{
    AudioSource interface = core1_source_interface(source);
    interface.open = open;
    return interface;
}

static bool open_mp3(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    Mp3Stream mp3;
    if (!mp3_probe(source->input, &mp3)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t playable = mp3_playable_samples(&mp3);
    uint32_t target = (uint32_t)((uint64_t)start_ms * mp3.sample_rate / MILLISECONDS_PER_SECOND);
    target = smaller(target, playable);
    source->start_offset =
        target == 0 ? mp3.audio_start : mp3_seek_offset(&mp3, target + mp3.lead_trim);
    target += mp3.lead_trim;
    bool from_start = source->start_offset == mp3.audio_start;
    source->end_offset = mp3.audio_end;
    source->written = source->start_offset;
    source->lead_trim = mp3.lead_trim;
    source->play_from = 0;
    source->play_end = from_start ? mp3.total_samples - mp3.tail_trim : UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = MP3_OUTPUT_BITS;
    source->channels = mp3.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = mp3.sample_rate,
                                         .channels = mp3.channels,
                                         .bits_per_sample = 0,
                                         .min_block = mp3.samples_per_frame,
                                         .max_block = mp3.samples_per_frame,
                                         .total_samples = playable},
                              .audio_bytes = mp3.audio_end - mp3.audio_start,
                              .codec = AUDIO_CODEC_MP3};
    volatile StreamControl *control = source->region.control;
    control->sample_rate = mp3.sample_rate;
    control->channels = mp3.channels;
    control->bits_per_sample = MP3_OUTPUT_BITS;
    control->min_block = mp3.samples_per_frame;
    control->max_block = mp3.samples_per_frame;
    control->total_samples = playable;
    control->first_sample = from_start ? 0 : target;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    control->codec = STREAM_CODEC_NATIVE;
    core1_source_use_ring(source, source->ring_capacity);
    core1_source_begin(source, track);
    return true;
}

AudioSource core1_source_mp3_interface(Core1Source *source)
{
    return opened_by(source, open_mp3);
}

static bool open_wav(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    WavFormat format = {0};
    if (!wav_parse(source->input, &format)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t start = (uint32_t)((uint64_t)start_ms * format.sample_rate / MILLISECONDS_PER_SECOND);
    start = smaller(start, format.frames);
    source->start_offset = format.data_offset + start * format.frame_bytes;
    source->end_offset = format.data_offset + format.frames * format.frame_bytes;
    source->written = source->start_offset;
    source->lead_trim = 0;
    source->play_from = 0;
    source->play_end = UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = format.bits;
    source->channels = format.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = format.sample_rate,
                                         .channels = format.channels,
                                         .bits_per_sample = format.bits,
                                         .total_samples = format.frames},
                              .audio_bytes = format.frames * format.frame_bytes,
                              .codec = AUDIO_CODEC_WAV};
    volatile StreamControl *control = source->region.control;
    control->sample_rate = format.sample_rate;
    control->channels = format.channels;
    control->bits_per_sample = format.bits;
    control->min_block = core1_pcm_block_frames(format.frame_bytes);
    control->max_block = control->min_block;
    control->total_samples = format.frames;
    control->first_sample = start;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    control->codec = STREAM_CODEC_PCM;
    core1_source_use_ring(source, source->ring_capacity);
    core1_source_begin(source, track);
    return true;
}

AudioSource core1_source_wav_interface(Core1Source *source)
{
    return opened_by(source, open_wav);
}

static AudioCodec audio_codec_of(Mp4Codec codec)
{
    return codec == MP4_CODEC_AAC ? AUDIO_CODEC_AAC : AUDIO_CODEC_ALAC;
}

static uint32_t stream_codec_of(Mp4Codec codec)
{
    return codec == MP4_CODEC_AAC ? STREAM_CODEC_AAC : STREAM_CODEC_ALAC;
}

static bool open_mp4(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    Mp4Track probed;
    uint32_t first = 0;
    if (!mp4_probe(source->input, &probed)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t playable = probed.total_samples - probed.lead_trim;
    uint32_t target = (uint32_t)((uint64_t)start_ms * probed.sample_rate / MILLISECONDS_PER_SECOND);
    target = smaller(target, playable) + probed.lead_trim;
    uint32_t preroll = probed.codec == MP4_CODEC_AAC ? probed.frame_samples : 0;
    uint32_t from = target > preroll ? target - preroll : 0;
    if (!mp4_stream_open(&source->mp4, source->input, &probed, from, &first)) {
        core1_source_stop(source);
        return false;
    }
    source->input = mp4_stream_input(&source->mp4);
    source->start_offset = 0;
    source->end_offset = source->input.size;
    source->written = 0;
    source->lead_trim = probed.lead_trim;
    source->play_from = 0;
    source->play_end = probed.total_samples;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample =
        probed.codec == MP4_CODEC_AAC ? MP3_OUTPUT_BITS : probed.bits_per_sample;
    source->channels = probed.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = probed.sample_rate,
                                         .channels = probed.channels,
                                         .bits_per_sample = probed.bits_per_sample,
                                         .min_block = (uint16_t)probed.frame_samples,
                                         .max_block = (uint16_t)probed.frame_samples,
                                         .total_samples = playable},
                              .audio_bytes = source->mp4.data_bytes,
                              .codec = audio_codec_of(probed.codec)};
    volatile StreamControl *control = source->region.control;
    control->sample_rate = probed.sample_rate;
    control->channels = probed.channels;
    control->bits_per_sample = source->bits_per_sample;
    control->min_block = probed.frame_samples;
    control->max_block = probed.frame_samples;
    control->total_samples = probed.total_samples;
    control->first_sample = first;
    control->start_offset = 0;
    control->end_offset = source->end_offset;
    control->written = 0;
    control->codec = stream_codec_of(probed.codec);
    core1_source_use_ring(source, source->ring_capacity);
    core1_source_begin(source, track);
    return true;
}

AudioSource core1_source_mp4_interface(Core1Source *source)
{
    return opened_by(source, open_mp4);
}

static bool open_ogg(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    OggOpusInfo info;
    if (!ogg_opus_probe(source->input, &info)) {
        core1_source_stop(source);
        return false;
    }
    uint32_t playable = info.last_granule - info.pre_skip;
    uint32_t target = (uint32_t)((uint64_t)start_ms * OGG_OPUS_RATE / MILLISECONDS_PER_SECOND);
    target = smaller(target, playable) + info.pre_skip;
    source->start_offset = ogg_opus_seek(source->input, &info, target);
    source->end_offset = source->input.size;
    source->written = source->start_offset;
    source->lead_trim = info.pre_skip;
    source->play_from = target;
    source->play_end = info.last_granule;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = OPUS_OUTPUT_BITS;
    source->channels = info.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = OGG_OPUS_RATE,
                                         .channels = info.channels,
                                         .bits_per_sample = 0,
                                         .min_block = OPUS_FRAME_SAMPLES,
                                         .max_block = OPUS_FRAME_SAMPLES,
                                         .total_samples = playable},
                              .audio_bytes = source->input.size - info.audio_start,
                              .codec = AUDIO_CODEC_OPUS};
    volatile StreamControl *control = source->region.control;
    control->sample_rate = OGG_OPUS_RATE;
    control->channels = info.channels;
    control->bits_per_sample = OPUS_OUTPUT_BITS;
    control->min_block = OPUS_FRAME_SAMPLES;
    control->max_block = OPUS_FRAME_SAMPLES;
    control->total_samples = info.last_granule;
    control->first_sample = 0;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    control->codec = STREAM_CODEC_OPUS;
    core1_source_use_ring(source, smaller(source->ring_capacity, CORE1_OPUS_RING_BYTES));
    core1_source_begin(source, track);
    return true;
}

AudioSource core1_source_ogg_interface(Core1Source *source)
{
    return opened_by(source, open_ogg);
}

static bool open_qoa(void *context, const char *path, uint32_t start_ms, AudioTrackInfo *track)
{
    Core1Source *source = context;
    core1_source_stop(source);
    FlacFileOpener files = source->flac.files;
    if (!files.open(files.context, path, &source->input)) {
        return false;
    }
    source->file_open = true;
    QoaInfo info;
    if (qoa_parse_header(source->input, &info) != QOA_OK) {
        core1_source_stop(source);
        return false;
    }
    uint32_t playable = info.total_samples;
    uint32_t target = (uint32_t)((uint64_t)start_ms * info.sample_rate / MILLISECONDS_PER_SECOND);
    target = smaller(target, playable - 1);
    uint32_t offset = 0;
    uint32_t first = 0;
    if (!qoa_frame_at(source->input, &info, target, &offset, &first)) {
        core1_source_stop(source);
        return false;
    }
    source->start_offset = offset;
    source->end_offset = source->input.size;
    source->written = offset;
    source->lead_trim = 0;
    source->play_from = 0;
    source->play_end = UINT32_MAX;
    source->slot = 0;
    source->read_failed = false;
    source->bits_per_sample = QOA_BITS_PER_SAMPLE;
    source->channels = info.channels;
    *track = (AudioTrackInfo){.stream = {.sample_rate = info.sample_rate,
                                         .channels = info.channels,
                                         .bits_per_sample = QOA_BITS_PER_SAMPLE,
                                         .min_block = info.frame_samples,
                                         .max_block = info.frame_samples,
                                         .total_samples = playable},
                              .audio_bytes = source->input.size - QOA_FILE_HEADER_BYTES,
                              .codec = AUDIO_CODEC_QOA};
    volatile StreamControl *control = source->region.control;
    control->sample_rate = info.sample_rate;
    control->channels = info.channels;
    control->bits_per_sample = QOA_BITS_PER_SAMPLE;
    control->min_block = info.frame_samples;
    control->max_block = info.frame_samples;
    control->total_samples = playable;
    control->first_sample = first;
    control->start_offset = source->start_offset;
    control->end_offset = source->end_offset;
    control->written = source->written;
    control->codec = STREAM_CODEC_QOA;
    core1_source_use_ring(source, source->ring_capacity);
    core1_source_begin(source, track);
    return true;
}

AudioSource core1_source_qoa_interface(Core1Source *source)
{
    return opened_by(source, open_qoa);
}

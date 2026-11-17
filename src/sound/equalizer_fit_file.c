#include "sound/equalizer_fit_file.h"

#include "equalizer_fit_layout.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    AUDIBLE_NUMERATOR = 9,
    AUDIBLE_DENOMINATOR = 20
};

uint32_t equalizer_curve_key(const EqualizerCurve *curve)
{
    uint8_t bytes[8];
    bytes_put32(bytes, FIT_METHOD);
    bytes_put32(bytes + 4, equalizer_target_rate(curve));
    uint32_t key = hash_update(HASH_START, bytes, sizeof bytes);
    int count = curve->band_count < EQUALIZER_MAX_BANDS ? curve->band_count : EQUALIZER_MAX_BANDS;
    for (int index = 0; index < count; index++) {
        EqualizerBand band = curve->bands[index];
        bytes_put16(bytes, band.frequency_hz);
        bytes_put16(bytes + 2, (uint16_t)band.gain_tenths);
        bytes_put16(bytes + 4, band.q_hundredths);
        bytes[6] = band.filter;
        bytes[7] = (uint8_t)index;
        key = hash_update(key, bytes, sizeof bytes);
    }
    return key;
}

bool equalizer_curve_fittable(const EqualizerCurve *curve)
{
    uint32_t target = equalizer_target_rate(curve);
    int count = curve->band_count < EQUALIZER_MAX_BANDS ? curve->band_count : EQUALIZER_MAX_BANDS;
    for (int index = 0; index < count; index++) {
        EqualizerBand band = curve->bands[index];
        if (band.gain_tenths != 0 && band.frequency_hz > 0 && band.q_hundredths > 0 &&
            band.filter <= EQUALIZER_HIGH_SHELF &&
            (uint32_t)band.frequency_hz * AUDIBLE_DENOMINATOR < target * AUDIBLE_NUMERATOR) {
            return true;
        }
    }
    return false;
}

uint32_t equalizer_fit_file_size(uint32_t entries)
{
    return EQUALIZER_FIT_FILE_HEADER + entries * EQUALIZER_FIT_FILE_ENTRY +
           EQUALIZER_FIT_FILE_CHECKSUM;
}

uint32_t equalizer_fit_file_checksum_start(void)
{
    return HASH_START;
}

uint32_t equalizer_fit_file_checksum(uint32_t running, const uint8_t *bytes, uint32_t length)
{
    return hash_update(running, bytes, length);
}

uint32_t equalizer_fit_file_stored_checksum(const uint8_t *bytes)
{
    return bytes_get32(bytes);
}

bool equalizer_fit_file_read_header(const uint8_t *bytes, EqualizerFitHeader *header)
{
    for (int index = 0; index < FIT_MAGIC_SIZE; index++) {
        if (bytes[index] != FIT_MAGIC[index]) {
            return false;
        }
    }
    if (bytes[VERSION_FIELD] != FIT_VERSION || bytes[ENTRIES_FIELD] > EQUALIZER_FIT_RATES) {
        return false;
    }
    *header = (EqualizerFitHeader){.key = bytes_get32(bytes + KEY_FIELD),
                                   .target_rate = bytes_get32(bytes + TARGET_FIELD),
                                   .entries = bytes[ENTRIES_FIELD]};
    return true;
}

bool equalizer_fit_file_read_entry(const uint8_t *bytes, uint32_t target_rate, EqualizerFit *fit)
{
    uint32_t count = bytes[COUNT_FIELD];
    uint32_t rate = bytes_get32(bytes + RATE_FIELD);
    if (count == 0 || count > EQUALIZER_MAX_BANDS || rate == 0 || rate > EQUALIZER_MAX_RATE) {
        return false;
    }
    *fit = (EqualizerFit){.rate = rate,
                          .target_rate = target_rate,
                          .worst_hundredths = (uint16_t)bytes_get16(bytes + WORST_FIELD),
                          .stage_count = (uint8_t)count};
    for (uint32_t index = 0; index < count; index++) {
        const uint8_t *stage = bytes + STAGES_FIELD + index * EQUALIZER_FIT_FILE_STAGE;
        fit->stages[index] = (EqualizerStage){.g = (int32_t)bytes_get32(stage + STAGE_G),
                                              .a1 = (int32_t)bytes_get32(stage + STAGE_A1),
                                              .m1 = (int32_t)bytes_get32(stage + STAGE_M1),
                                              .m2 = (int32_t)bytes_get32(stage + STAGE_M2),
                                              .band = (uint8_t)(stage[STAGE_BAND] & ~FINE_FLAG),
                                              .fine = (stage[STAGE_BAND] & FINE_FLAG) != 0};
        if (fit->stages[index].band >= EQUALIZER_MAX_BANDS) {
            return false;
        }
    }
    return true;
}

uint32_t equalizer_fit_file_entry_rate(const uint8_t *bytes)
{
    return bytes_get32(bytes + RATE_FIELD);
}

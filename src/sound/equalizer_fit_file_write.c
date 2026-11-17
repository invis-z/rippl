#include "sound/equalizer_fit_file.h"

#include "equalizer_fit_layout.h"

#include <stddef.h>

#include "core/hash.h"

void equalizer_fit_file_write_header(uint8_t *bytes, EqualizerFitHeader header)
{
    for (int index = 0; index < EQUALIZER_FIT_FILE_HEADER; index++) {
        bytes[index] = index < FIT_MAGIC_SIZE ? FIT_MAGIC[index] : 0;
    }
    bytes[VERSION_FIELD] = FIT_VERSION;
    bytes[ENTRIES_FIELD] = (uint8_t)header.entries;
    bytes_put32(bytes + KEY_FIELD, header.key);
    bytes_put32(bytes + TARGET_FIELD, header.target_rate);
}

void equalizer_fit_file_write_entry(uint8_t *bytes, const EqualizerFit *fit)
{
    for (int index = 0; index < EQUALIZER_FIT_FILE_ENTRY; index++) {
        bytes[index] = 0;
    }
    bytes_put32(bytes + RATE_FIELD, fit->rate);
    bytes_put16(bytes + WORST_FIELD, fit->worst_hundredths);
    bytes[COUNT_FIELD] = fit->stage_count;
    for (int index = 0; index < fit->stage_count && index < EQUALIZER_MAX_BANDS; index++) {
        uint8_t *stage = bytes + STAGES_FIELD + index * EQUALIZER_FIT_FILE_STAGE;
        bytes_put32(stage + STAGE_G, (uint32_t)fit->stages[index].g);
        bytes_put32(stage + STAGE_A1, (uint32_t)fit->stages[index].a1);
        bytes_put32(stage + STAGE_M1, (uint32_t)fit->stages[index].m1);
        bytes_put32(stage + STAGE_M2, (uint32_t)fit->stages[index].m2);
        stage[STAGE_BAND] =
            (uint8_t)(fit->stages[index].band | (fit->stages[index].fine ? FINE_FLAG : 0));
    }
}

void equalizer_fit_file_seal(uint8_t *bytes, uint32_t entries)
{
    uint32_t length = equalizer_fit_file_size(entries) - EQUALIZER_FIT_FILE_CHECKSUM;
    bytes_put32(bytes + length, hash_update(HASH_START, bytes, length));
}

#include "sound/equalizer_fit_file.h"

#include <stddef.h>

#include "core/hash.h"

enum {
    VERSION = 1,
    FIT_METHOD = 2,
    MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    ENTRIES_FIELD = 5,
    KEY_FIELD = 8,
    TARGET_FIELD = 12,
    RATE_FIELD = 0,
    WORST_FIELD = 4,
    COUNT_FIELD = 6,
    STAGES_FIELD = 8,
    STAGE_G = 0,
    STAGE_A1 = 4,
    STAGE_M1 = 8,
    STAGE_M2 = 12,
    STAGE_BAND = 16,
    FINE_FLAG = 0x80,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'Q', 'F', 'T'};

static void put32(uint8_t *bytes, uint32_t value)
{
    for (int index = 0; index < 4; index++) {
        bytes[index] = (uint8_t)(value >> (BITS_PER_BYTE * index));
    }
}

static void put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> BITS_PER_BYTE);
}

void equalizer_fit_file_write_header(uint8_t *bytes, EqualizerFitHeader header)
{
    for (int index = 0; index < EQUALIZER_FIT_FILE_HEADER; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    bytes[VERSION_FIELD] = VERSION;
    bytes[ENTRIES_FIELD] = (uint8_t)header.entries;
    put32(bytes + KEY_FIELD, header.key);
    put32(bytes + TARGET_FIELD, header.target_rate);
}

void equalizer_fit_file_write_entry(uint8_t *bytes, const EqualizerFit *fit)
{
    for (int index = 0; index < EQUALIZER_FIT_FILE_ENTRY; index++) {
        bytes[index] = 0;
    }
    put32(bytes + RATE_FIELD, fit->rate);
    put16(bytes + WORST_FIELD, fit->worst_hundredths);
    bytes[COUNT_FIELD] = fit->stage_count;
    for (int index = 0; index < fit->stage_count && index < EQUALIZER_MAX_BANDS; index++) {
        uint8_t *stage = bytes + STAGES_FIELD + index * EQUALIZER_FIT_FILE_STAGE;
        put32(stage + STAGE_G, (uint32_t)fit->stages[index].g);
        put32(stage + STAGE_A1, (uint32_t)fit->stages[index].a1);
        put32(stage + STAGE_M1, (uint32_t)fit->stages[index].m1);
        put32(stage + STAGE_M2, (uint32_t)fit->stages[index].m2);
        stage[STAGE_BAND] =
            (uint8_t)(fit->stages[index].band | (fit->stages[index].fine ? FINE_FLAG : 0));
    }
}

void equalizer_fit_file_seal(uint8_t *bytes, uint32_t entries)
{
    uint32_t length = equalizer_fit_file_size(entries) - EQUALIZER_FIT_FILE_CHECKSUM;
    put32(bytes + length, hash_update(HASH_START, bytes, length));
}

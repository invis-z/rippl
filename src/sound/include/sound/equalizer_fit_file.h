#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dsp/equalizer.h"
#include "dsp/equalizer_fit.h"

enum {
    EQUALIZER_FIT_FILE_HEADER = 16,
    EQUALIZER_FIT_FILE_STAGE = 17,
    EQUALIZER_FIT_FILE_ENTRY = 8 + EQUALIZER_FIT_FILE_STAGE * EQUALIZER_MAX_BANDS,
    EQUALIZER_FIT_FILE_CHECKSUM = 4,
    EQUALIZER_FIT_FILE_MAX = EQUALIZER_FIT_FILE_HEADER +
        EQUALIZER_FIT_RATES * EQUALIZER_FIT_FILE_ENTRY + EQUALIZER_FIT_FILE_CHECKSUM
};

typedef struct EqualizerFitHeader {
    uint32_t key;
    uint32_t target_rate;
    uint32_t entries;
} EqualizerFitHeader;

uint32_t equalizer_curve_key(const EqualizerCurve *curve);
bool equalizer_curve_fittable(const EqualizerCurve *curve);
uint32_t equalizer_fit_file_size(uint32_t entries);
void equalizer_fit_file_write_header(uint8_t *bytes, EqualizerFitHeader header);
void equalizer_fit_file_write_entry(uint8_t *bytes, const EqualizerFit *fit);
void equalizer_fit_file_seal(uint8_t *bytes, uint32_t entries);
bool equalizer_fit_file_read_header(const uint8_t *bytes, EqualizerFitHeader *header);
uint32_t equalizer_fit_file_entry_rate(const uint8_t *bytes);
bool equalizer_fit_file_read_entry(const uint8_t *bytes, uint32_t target_rate, EqualizerFit *fit);
uint32_t equalizer_fit_file_checksum(uint32_t running, const uint8_t *bytes, uint32_t length);
uint32_t equalizer_fit_file_checksum_start(void);
uint32_t equalizer_fit_file_stored_checksum(const uint8_t *bytes);

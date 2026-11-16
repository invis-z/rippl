#pragma once

#include <stdint.h>

#include "dsp/equalizer.h"

enum {
    EQUALIZER_BAND_COEFFICIENT_BITS = 14,
    EQUALIZER_BAND_COEFFICIENT_MAX = (1 << 16) - 1,
    EQUALIZER_BAND_M1_BITS = 13,
    EQUALIZER_BAND_M1_MAX = (1 << 18) - 1,
    EQUALIZER_BAND_RESPONSE_BITS = 16,
    EQUALIZER_BAND_TABLE_BITS = 30
};

static const int64_t EQUALIZER_BAND_ONE = INT64_C(1) << EQUALIZER_BAND_TABLE_BITS;
static const int64_t EQUALIZER_BAND_R_ONE = INT64_C(1) << EQUALIZER_BAND_RESPONSE_BITS;

typedef struct EqualizerShape {
    int64_t g;
    int64_t k;
    int64_t m1;
    int64_t m2;
} EqualizerShape;

static inline int64_t equalizer_band_multiply(int64_t first, int64_t second)
{
    return (first * second + (INT64_C(1) << (EQUALIZER_BAND_TABLE_BITS - 1))) >>
           EQUALIZER_BAND_TABLE_BITS;
}

static inline int64_t equalizer_band_divided(int64_t numerator, int64_t denominator)
{
    return (numerator + denominator / 2) / denominator;
}

static inline int equalizer_band_clamped_gain(int gain_tenths)
{
    return gain_tenths < EQUALIZER_GAIN_MIN_TENTHS   ? EQUALIZER_GAIN_MIN_TENTHS
           : gain_tenths > EQUALIZER_GAIN_MAX_TENTHS ? EQUALIZER_GAIN_MAX_TENTHS
                                                     : gain_tenths;
}

int64_t equalizer_band_amplitude(int hundredths);
int64_t equalizer_band_times(int64_t first, int64_t second);
EqualizerStage equalizer_band_design(EqualizerBand band, uint32_t rate);
EqualizerShape equalizer_band_shape(const EqualizerStage *stage);
int64_t equalizer_band_response_squared(const EqualizerShape *shape, int64_t tangent);

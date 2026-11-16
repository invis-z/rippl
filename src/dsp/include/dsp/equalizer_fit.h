#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dsp/equalizer.h"

enum {
    EQUALIZER_FIT_RATES = 11,
    EQUALIZER_FIT_SPACED_STARTS = 2,
    EQUALIZER_FIT_ROUNDS = 200
};

extern const uint32_t EQUALIZER_FIT_RATE_LIST[EQUALIZER_FIT_RATES];

typedef struct EqualizerFitStats {
    uint32_t rounds;
    uint32_t starts;
    uint16_t today_hundredths;
    uint16_t fit_hundredths;
    uint32_t slope_evaluations;
    uint32_t plain_evaluations;
    uint32_t fixed_evaluations;
} EqualizerFitStats;

uint32_t equalizer_fit_work_size(void);
bool equalizer_fit_needed(const EqualizerCurve *curve, uint32_t rate);
typedef void (*EqualizerFitBeat)(void);

typedef struct EqualizerFitPart {
    uint32_t part;
    uint32_t parts;
    EqualizerFitBeat beat;
    const EqualizerFit *seed;
} EqualizerFitPart;

uint32_t equalizer_fit_starts(const EqualizerCurve *curve, uint32_t rate, const EqualizerFit *seed);
bool equalizer_fit_part(const EqualizerCurve *curve, uint32_t rate, void *work, uint32_t size,
                        EqualizerFitPart part, EqualizerFit *candidate, EqualizerFitStats *stats);

bool equalizer_fit(const EqualizerCurve *curve, uint32_t rate, void *work, uint32_t size,
                   EqualizerFit *fit, EqualizerFitStats *stats, EqualizerFitBeat beat,
                   const EqualizerFit *seed);

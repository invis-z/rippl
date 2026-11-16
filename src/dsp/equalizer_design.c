#include "dsp/equalizer.h"

#include <stddef.h>

#include "equalizer_band.h"

enum {
    RESPONSE_BITS = EQUALIZER_BAND_RESPONSE_BITS,
    STATE_LIMIT = 12,
    DOWN_ONE_DB_HUNDREDTHS = -200,
    TABLE_BITS = EQUALIZER_BAND_TABLE_BITS,
    TENTHS = 10,
    PREAMP_FLOOR_TENTHS = -480
};

static const int64_t ONE = EQUALIZER_BAND_ONE;
static const int64_t R_ONE = EQUALIZER_BAND_R_ONE;

typedef EqualizerShape Shape;

static int64_t multiply(int64_t first, int64_t second)
{
    return equalizer_band_multiply(first, second);
}

static int64_t divided(int64_t numerator, int64_t denominator)
{
    return equalizer_band_divided(numerator, denominator);
}

static int64_t times(int64_t first, int64_t second)
{
    return equalizer_band_times(first, second);
}

static int clamped_gain(int gain_tenths)
{
    return equalizer_band_clamped_gain(gain_tenths);
}

static int clamped_preamp(int preamp_tenths)
{
    return preamp_tenths < EQUALIZER_PREAMP_MIN_TENTHS   ? EQUALIZER_PREAMP_MIN_TENTHS
           : preamp_tenths > EQUALIZER_PREAMP_MAX_TENTHS ? EQUALIZER_PREAMP_MAX_TENTHS
                                                         : preamp_tenths;
}

static int64_t amplitude_for_gain_tenths(int gain_tenths)
{
    return equalizer_band_amplitude(clamped_gain(gain_tenths) * TENTHS);
}

static int64_t linear_for_preamp_tenths(int preamp_tenths)
{
    int value = preamp_tenths < PREAMP_FLOOR_TENTHS ? PREAMP_FLOOR_TENTHS
                : preamp_tenths > 0                 ? 0
                                                    : preamp_tenths;
    int64_t lower = multiply(amplitude_for_gain_tenths(EQUALIZER_GAIN_MIN_TENTHS),
                             amplitude_for_gain_tenths(EQUALIZER_GAIN_MIN_TENTHS));
    int64_t factor = ONE;
    while (value < EQUALIZER_GAIN_MIN_TENTHS) {
        factor = multiply(factor, lower);
        value -= EQUALIZER_GAIN_MIN_TENTHS;
    }
    int64_t amplitude = amplitude_for_gain_tenths(value);
    return multiply(factor, multiply(amplitude, amplitude));
}

bool equalizer_curve_valid(const EqualizerCurve *curve, uint32_t rate)
{
    return curve->band_count <= EQUALIZER_MAX_BANDS && rate > 0 && rate <= EQUALIZER_MAX_RATE;
}

int equalizer_preamp_tenths(const EqualizerCurve *curve, uint32_t rate)
{
    int boost = 0;
    for (int index = 0; index < curve->band_count; index++) {
        EqualizerBand band = curve->bands[index];
        int gain = clamped_gain(band.gain_tenths);
        if (equalizer_band_audible(band, rate) && gain > boost) {
            boost = gain;
        }
    }
    int automatic = -boost;
    if (!curve->preamp_set) {
        return automatic;
    }
    int supplied = clamped_preamp(curve->preamp_tenths);
    return supplied < automatic ? supplied : automatic;
}

static int shelf_lift_tenths(const EqualizerCurve *curve, uint32_t rate)
{
    int lift = 0;
    for (int index = 0; index < curve->band_count; index++) {
        EqualizerBand band = curve->bands[index];
        if (band.filter == EQUALIZER_HIGH_SHELF && equalizer_band_audible(band, rate)) {
            lift += clamped_gain(band.gain_tenths);
        }
    }
    return lift;
}

static void clear_histories(EqualizerChain *chain)
{
    for (int band = 0; band < EQUALIZER_MAX_BANDS; band++) {
        for (int index = 0; index < EQUALIZER_HISTORY; index++) {
            chain->left[band][index] = 0;
            chain->right[band][index] = 0;
        }
    }
}

static int64_t state_peak_squared(const Shape *shape)
{
    int64_t g_squared = (shape->g * shape->g) >> RESPONSE_BITS;
    int64_t k = shape->k > 0 ? shape->k : 1;
    int64_t peak = R_ONE + shape->g + divided((R_ONE + g_squared) << RESPONSE_BITS, k);
    return times(peak, peak);
}

static int64_t largest(int64_t first, int64_t second)
{
    return first > second ? first : second;
}

static int64_t worst_peak_squared(const EqualizerChain *chain)
{
    Shape shapes[EQUALIZER_MAX_BANDS];
    int64_t worst = R_ONE;
    for (int index = 0; index < chain->stage_count; index++) {
        shapes[index] = equalizer_band_shape(&chain->stages[index]);
        worst = times(worst, times(R_ONE + shapes[index].m2, R_ONE + shapes[index].m2));
    }
    for (int stage = 0; stage < chain->stage_count; stage++) {
        int64_t gain = R_ONE;
        for (int index = 0; index < chain->stage_count; index++) {
            if (index == stage) {
                worst = largest(worst, times(gain, state_peak_squared(&shapes[stage])));
            }
            gain = times(gain, equalizer_band_response_squared(&shapes[index], shapes[stage].g));
        }
        worst = largest(worst, gain);
    }
    return worst;
}

static int32_t within_headroom(const EqualizerChain *chain, int64_t preamp)
{
    int64_t worst = worst_peak_squared(chain);
    int64_t limit = (int64_t)STATE_LIMIT * STATE_LIMIT * R_ONE;
    while (preamp > 0) {
        int64_t level = preamp >> (TABLE_BITS - RESPONSE_BITS);
        if (times(times(level, level), worst) <= limit) {
            break;
        }
        preamp = multiply(preamp, equalizer_band_amplitude(DOWN_ONE_DB_HUNDREDTHS));
    }
    return (int32_t)preamp;
}

static void design_chain(EqualizerChain *chain, const EqualizerCurve *curve, uint32_t rate,
                         bool keep_history)
{
    chain->curve = *curve;
    if (chain->curve.band_count > EQUALIZER_MAX_BANDS) {
        chain->curve.band_count = EQUALIZER_MAX_BANDS;
    }
    chain->stage_count = 0;
    chain->preamp = (int32_t)ONE;
    chain->valid = 0;
    if (!keep_history) {
        clear_histories(chain);
    }
    if (rate == 0 || rate > EQUALIZER_MAX_RATE) {
        return;
    }
    if (!equalizer_curve_valid(&chain->curve, rate)) {
        return;
    }
    chain->valid = 1;
    for (int band = 0; band < chain->curve.band_count; band++) {
        if (!equalizer_band_audible(chain->curve.bands[band], rate)) {
            continue;
        }
        EqualizerStage *stage = &chain->stages[chain->stage_count++];
        *stage = equalizer_band_design(chain->curve.bands[band], rate);
        stage->band = (uint8_t)band;
    }
    int preamp =
        equalizer_preamp_tenths(&chain->curve, rate) + shelf_lift_tenths(&chain->curve, rate);
    chain->preamp = within_headroom(chain, linear_for_preamp_tenths(preamp));
}

static bool same_layout(const EqualizerCurve *first, const EqualizerCurve *second)
{
    if (first->band_count != second->band_count) {
        return false;
    }
    for (int index = 0; index < first->band_count; index++) {
        if (first->bands[index].frequency_hz != second->bands[index].frequency_hz ||
            first->bands[index].filter != second->bands[index].filter) {
            return false;
        }
    }
    return true;
}

static bool chain_active(const EqualizerChain *chain)
{
    return chain->stage_count > 0 || chain->preamp != (int32_t)ONE;
}

void equalizer_init(Equalizer *equalizer)
{
    *equalizer = (Equalizer){0};
}

void equalizer_reset(Equalizer *equalizer)
{
    EqualizerCurve curve =
        equalizer->has_pending ? equalizer->pending_curve : equalizer->current.curve;
    uint32_t rate = equalizer->rate;
    *equalizer = (Equalizer){0};
    equalizer->rate = rate;
    design_chain(&equalizer->current, &curve, rate, false);
}

void equalizer_set_curve(Equalizer *equalizer, const EqualizerCurve *curve)
{
    bool keep_history = !equalizer->has_pending && chain_active(&equalizer->current) &&
                        same_layout(&equalizer->current.curve, curve);
    if (keep_history) {
        design_chain(&equalizer->current, curve, equalizer->rate, true);
        return;
    }
    if (!equalizer->has_pending && !chain_active(&equalizer->current)) {
        design_chain(&equalizer->current, curve, equalizer->rate, false);
        return;
    }
    if (!equalizer->has_pending) {
        equalizer->fade_remaining = EQUALIZER_FADE_FRAMES;
    } else if (!equalizer->fading_out) {
        equalizer->fade_remaining = (uint16_t)(EQUALIZER_FADE_FRAMES - equalizer->fade_remaining);
    }
    equalizer->pending_curve = *curve;
    equalizer->has_pending = 1;
    equalizer->fading_out = 1;
}

void equalizer_apply_pending(Equalizer *equalizer)
{
    design_chain(&equalizer->current, &equalizer->pending_curve, equalizer->rate, false);
    equalizer->fading_out = 0;
    equalizer->fade_remaining = EQUALIZER_FADE_FRAMES;
}

void equalizer_set_rate(Equalizer *equalizer, uint32_t rate)
{
    if (equalizer->rate == rate) {
        return;
    }
    equalizer->rate = rate;
    if (equalizer->has_pending) {
        design_chain(&equalizer->current, &equalizer->pending_curve, rate, false);
        equalizer->has_pending = 0;
        equalizer->fading_out = 0;
        equalizer->fade_remaining = 0;
        return;
    }
    design_chain(&equalizer->current, &equalizer->current.curve, rate, false);
}

bool equalizer_active(const Equalizer *equalizer)
{
    return chain_active(&equalizer->current) || equalizer->has_pending;
}

bool equalizer_valid(const Equalizer *equalizer)
{
    return equalizer->current.valid != 0;
}

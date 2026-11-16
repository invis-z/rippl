#include "equalizer_band.h"

#include <stdbool.h>

enum {
    COEFFICIENT_BITS = EQUALIZER_BAND_COEFFICIENT_BITS,
    COEFFICIENT_MAX = EQUALIZER_BAND_COEFFICIENT_MAX,
    M1_BITS = EQUALIZER_BAND_M1_BITS,
    M1_MAX = EQUALIZER_BAND_M1_MAX,
    RESPONSE_BITS = EQUALIZER_BAND_RESPONSE_BITS,
    PRODUCT_SHIFT = 8,
    TABLE_BITS = EQUALIZER_BAND_TABLE_BITS,
    GAIN_TABLE_OFFSET = 24,
    SINE_LAST_TERM = 12,
    COSINE_LAST_TERM = 13,
    AUDIBLE_NUMERATOR = 9,
    AUDIBLE_DENOMINATOR = 20,
    TENTHS = 10,
    HUNDREDTHS = 100,
    TABLE_LAST = 36
};

static const int64_t ONE = EQUALIZER_BAND_ONE;
static const int64_t R_ONE = EQUALIZER_BAND_R_ONE;
static const int64_t R_CAP = INT64_C(1) << 38;
static const int64_t R_WIDE = INT64_C(1) << 31;
static const int64_t OMEGA_MIN = INT64_C(1) << (RESPONSE_BITS - 6);
static const int64_t OMEGA_MAX = INT64_C(1) << (RESPONSE_BITS + 6);
static const uint64_t PI_Q30 = UINT64_C(3373259426);

static const int32_t GAIN_Q30[] = {
    269711752,  285693178,  302621563,  320553018,  339546978,  359666402,  380977976,  403552340,
    427464319,  452793173,  479622855,  508042296,  538145694,  570032831,  603809400,  639587356,
    677485290,  717628817,  760150998,  805192776,  852903448,  903441154,  956973408,  1013677647,
    1073741824, 1137365027, 1204758142, 1276144550, 1351760868, 1431857735, 1516700640, 1606570803,
    1701766107, 1802602089, 1909412977, 2022552809, 2142396597};

typedef struct Angle {
    int64_t sine;
    int64_t cosine;
} Angle;

static int64_t multiply(int64_t first, int64_t second)
{
    return equalizer_band_multiply(first, second);
}

static int64_t divided(int64_t numerator, int64_t denominator)
{
    return equalizer_band_divided(numerator, denominator);
}

static int64_t product(int64_t first, int64_t second)
{
    return divided((first >> PRODUCT_SHIFT) * (second >> PRODUCT_SHIFT),
                   INT64_C(1) << (TABLE_BITS - 2 * PRODUCT_SHIFT));
}

static int clamped_q(int q_hundredths)
{
    return q_hundredths < EQUALIZER_Q_MIN_HUNDREDTHS   ? EQUALIZER_Q_MIN_HUNDREDTHS
           : q_hundredths > EQUALIZER_Q_MAX_HUNDREDTHS ? EQUALIZER_Q_MAX_HUNDREDTHS
                                                       : q_hundredths;
}

int64_t equalizer_band_amplitude(int hundredths)
{
    int scaled = hundredths + GAIN_TABLE_OFFSET * HUNDREDTHS;
    scaled = scaled < 0 ? 0 : scaled > TABLE_LAST * HUNDREDTHS ? TABLE_LAST * HUNDREDTHS : scaled;
    int index = scaled / HUNDREDTHS;
    int fraction = scaled % HUNDREDTHS;
    int64_t low = GAIN_Q30[index];
    if (fraction == 0) {
        return low;
    }
    int64_t high = GAIN_Q30[index + 1];
    return low + divided((high - low) * fraction, HUNDREDTHS);
}

static int64_t amplitude_for_gain_tenths(int gain_tenths)
{
    return equalizer_band_amplitude(equalizer_band_clamped_gain(gain_tenths) * TENTHS);
}

static int64_t series(int64_t square, int last)
{
    int64_t term = ONE;
    for (int k = last; k > 0; k -= 2) {
        term = ONE - (int32_t)multiply(square, term) / (k * (k + 1));
    }
    return term;
}

static Angle half_angle_of(uint32_t frequency, uint32_t rate)
{
    int64_t half = (int64_t)(frequency * PI_Q30 / rate);
    int64_t square = multiply(half, half);
    return (Angle){.sine = multiply(half, series(square, SINE_LAST_TERM)),
                   .cosine = series(square, COSINE_LAST_TERM)};
}

bool equalizer_band_audible(EqualizerBand band, uint32_t rate)
{
    return band.gain_tenths != 0 && band.frequency_hz > 0 && band.q_hundredths > 0 &&
           band.filter <= EQUALIZER_HIGH_SHELF &&
           (uint32_t)band.frequency_hz * AUDIBLE_DENOMINATOR < rate * AUDIBLE_NUMERATOR;
}

static int32_t coefficient(int64_t value)
{
    int64_t rounded = divided(value, INT64_C(1) << (TABLE_BITS - COEFFICIENT_BITS));
    return rounded > COEFFICIENT_MAX    ? COEFFICIENT_MAX
           : rounded < -COEFFICIENT_MAX ? -COEFFICIENT_MAX
                                        : (int32_t)rounded;
}

static int32_t coefficient_m1(int64_t value)
{
    int64_t rounded = divided(value, INT64_C(1) << (TABLE_BITS - M1_BITS));
    return rounded > M1_MAX ? M1_MAX : rounded < -M1_MAX ? -M1_MAX : (int32_t)rounded;
}

static int64_t widened(int32_t value)
{
    return (int64_t)value << (TABLE_BITS - COEFFICIENT_BITS);
}

static int64_t effective_damping(int32_t a1, int64_t g)
{
    int64_t inverse = divided(INT64_C(1) << (TABLE_BITS + COEFFICIENT_BITS), a1);
    return divided((inverse - ONE) << (TABLE_BITS - 4), g >> 4) - g;
}

EqualizerStage equalizer_band_design(EqualizerBand band, uint32_t rate)
{
    Angle half = half_angle_of(band.frequency_hz, rate);
    int64_t tangent = divided(half.sine << TABLE_BITS, half.cosine);
    int gain = equalizer_band_clamped_gain(band.gain_tenths);
    if (band.filter == EQUALIZER_HIGH_SHELF) {
        gain = -gain;
    }
    int64_t amplitude = amplitude_for_gain_tenths(gain);
    int q_hundredths = clamped_q(band.q_hundredths);
    bool shelf = band.filter != EQUALIZER_PEAK;
    int64_t width = shelf ? divided(ONE * q_hundredths, HUNDREDTHS)
                          : divided(amplitude * q_hundredths, HUNDREDTHS);
    int64_t damping = divided(ONE << TABLE_BITS, width);
    if (shelf) {
        tangent = multiply(tangent, equalizer_band_amplitude(-gain * TENTHS / 2));
    }
    int32_t g = coefficient(tangent);
    int64_t g_wide = widened(g);
    int64_t denominator = ONE + product(g_wide, g_wide + damping);
    int32_t a1 = (int32_t)divided(INT64_C(1) << (TABLE_BITS + COEFFICIENT_BITS), denominator);
    int64_t squared_minus_one = product(amplitude, amplitude) - ONE;
    int64_t effective = effective_damping(a1, g_wide);
    if (!shelf) {
        return (EqualizerStage){
            .g = g, .a1 = a1, .m1 = coefficient_m1(product(effective, squared_minus_one))};
    }
    return (EqualizerStage){.g = g,
                            .a1 = a1,
                            .m1 = coefficient_m1(product(effective, amplitude - ONE)),
                            .m2 = coefficient(squared_minus_one)};
}

int64_t equalizer_band_times(int64_t first, int64_t second)
{
    int64_t larger = first > second ? first : second;
    int64_t smaller = first > second ? second : first;
    larger = larger > R_CAP ? R_CAP : larger;
    int64_t result =
        larger < R_WIDE ? (larger * smaller) >> RESPONSE_BITS : (larger >> RESPONSE_BITS) * smaller;
    return result > R_CAP ? R_CAP : result;
}

EqualizerShape equalizer_band_shape(const EqualizerStage *stage)
{
    int64_t g = widened(stage->g);
    return (EqualizerShape){.g = g >> (TABLE_BITS - RESPONSE_BITS),
                            .k = effective_damping(stage->a1, g) >> (TABLE_BITS - RESPONSE_BITS),
                            .m1 = (int64_t)stage->m1 * (1 << (RESPONSE_BITS - M1_BITS)),
                            .m2 = (int64_t)stage->m2 * (1 << (RESPONSE_BITS - COEFFICIENT_BITS))};
}

int64_t equalizer_band_response_squared(const EqualizerShape *shape, int64_t tangent)
{
    int64_t omega = divided(tangent << RESPONSE_BITS, shape->g);
    if (omega < OMEGA_MIN) {
        return equalizer_band_times(R_ONE + shape->m2, R_ONE + shape->m2);
    }
    if (omega > OMEGA_MAX) {
        return R_ONE;
    }
    int64_t w = (omega * omega) >> RESPONSE_BITS;
    int64_t real_top = R_ONE + shape->m2 - w;
    int64_t imaginary_top = ((shape->k + shape->m1) * omega) >> RESPONSE_BITS;
    int64_t real_bottom = R_ONE - w;
    int64_t imaginary_bottom = (shape->k * omega) >> RESPONSE_BITS;
    int64_t top = real_top * real_top + imaginary_top * imaginary_top;
    int64_t bottom =
        (real_bottom * real_bottom + imaginary_bottom * imaginary_bottom) >> RESPONSE_BITS;
    return top / (bottom > 0 ? bottom : 1);
}

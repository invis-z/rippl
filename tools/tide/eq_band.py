from dataclasses import dataclass

import numpy as np

from tide import eq_text

COEFFICIENT_BITS = 14
COEFFICIENT_MAX = (1 << 16) - 1
M1_BITS = 13
M1_MAX = (1 << 18) - 1
RESPONSE_BITS = 16
TABLE_BITS = 30
FINE_G_BITS = 20
FINE_C_BITS = 18
FINE_G_LIMIT = 1 << (31 - FINE_G_BITS)
FINE_C_LIMIT = 1 << (31 - FINE_C_BITS)
PRODUCT_SHIFT = 8
GAIN_TABLE_OFFSET = 24
SINE_LAST_TERM = 12
COSINE_LAST_TERM = 13
TENTHS = 10
HUNDREDTHS = 100
TABLE_LAST = 36

ONE = 1 << TABLE_BITS
R_ONE = 1 << RESPONSE_BITS
R_CAP = 1 << 38
R_WIDE = 1 << 31
OMEGA_MIN = 1 << (RESPONSE_BITS - 6)
OMEGA_MAX = 1 << (RESPONSE_BITS + 6)
PI_Q30 = 3373259426

GAIN_Q30 = (
    269711752, 285693178, 302621563, 320553018, 339546978, 359666402, 380977976, 403552340,
    427464319, 452793173, 479622855, 508042296, 538145694, 570032831, 603809400, 639587356,
    677485290, 717628817, 760150998, 805192776, 852903448, 903441154, 956973408, 1013677647,
    1073741824, 1137365027, 1204758142, 1276144550, 1351760868, 1431857735, 1516700640,
    1606570803, 1701766107, 1802602089, 1909412977, 2022552809, 2142396597)


@dataclass
class Stage:
    g: int
    a1: int
    m1: int = 0
    m2: int = 0
    band: int = 0
    fine: bool = False


@dataclass(frozen=True)
class Shape:
    g_wide: int
    g: int
    k: int
    m1: int
    m2: int


def truncated(numerator, denominator):
    quotient = abs(numerator) // abs(denominator)
    return quotient if (numerator >= 0) == (denominator > 0) else -quotient


def int32(value):
    return ((value + 0x80000000) & 0xFFFFFFFF) - 0x80000000


def multiply(first, second):
    return (first * second + (1 << (TABLE_BITS - 1))) >> TABLE_BITS


def divided(numerator, denominator):
    return truncated(numerator + truncated(denominator, 2), denominator)


def product(first, second):
    return divided((first >> PRODUCT_SHIFT) * (second >> PRODUCT_SHIFT),
                   1 << (TABLE_BITS - 2 * PRODUCT_SHIFT))


def clamped_gain(tenths):
    return min(max(tenths, eq_text.GAIN_MIN_TENTHS), eq_text.GAIN_MAX_TENTHS)


def clamped_q(hundredths):
    return min(max(hundredths, eq_text.Q_MIN_HUNDREDTHS), eq_text.Q_MAX_HUNDREDTHS)


def amplitude(hundredths):
    scaled = hundredths + GAIN_TABLE_OFFSET * HUNDREDTHS
    scaled = min(max(scaled, 0), TABLE_LAST * HUNDREDTHS)
    index = scaled // HUNDREDTHS
    fraction = scaled % HUNDREDTHS
    low = GAIN_Q30[index]
    if fraction == 0:
        return low
    return low + divided((GAIN_Q30[index + 1] - low) * fraction, HUNDREDTHS)


def _series(square, last):
    term = ONE
    for k in range(last, 0, -2):
        term = ONE - truncated(int32(multiply(square, term)), k * (k + 1))
    return term


def _half_angle(frequency, rate):
    half = (frequency * PI_Q30) // rate
    square = multiply(half, half)
    return multiply(half, _series(square, SINE_LAST_TERM)), _series(square, COSINE_LAST_TERM)


def audible(band, rate):
    return (band.gain_tenths != 0 and band.frequency_hz > 0 and band.q_hundredths > 0
            and band.filter <= eq_text.HIGH_SHELF and band.frequency_hz * 20 < rate * 9)


def _coefficient(value):
    rounded = divided(value, 1 << (TABLE_BITS - COEFFICIENT_BITS))
    return min(max(rounded, -COEFFICIENT_MAX), COEFFICIENT_MAX)


def _coefficient_m1(value):
    rounded = divided(value, 1 << (TABLE_BITS - M1_BITS))
    return min(max(rounded, -M1_MAX), M1_MAX)


def _widened(value):
    return value << (TABLE_BITS - COEFFICIENT_BITS)


def _effective_damping(a1, g):
    inverse = divided(1 << (TABLE_BITS + COEFFICIENT_BITS), a1)
    return divided((inverse - ONE) << (TABLE_BITS - 4), g >> 4) - g


def _core_of(tangent, damping):
    fine_g = divided(tangent, 1 << (TABLE_BITS - FINE_G_BITS))
    if 0 < fine_g < FINE_G_LIMIT:
        g_wide = fine_g << (TABLE_BITS - FINE_G_BITS)
        spread = product(g_wide, g_wide + damping)
        complement = divided(spread << TABLE_BITS, ONE + spread)
        fine_c = divided(complement, 1 << (TABLE_BITS - FINE_C_BITS))
        if 0 < fine_c < FINE_C_LIMIT:
            return Stage(g=int32(fine_g), a1=int32(fine_c), fine=True)
    g = _coefficient(tangent)
    g_wide = _widened(g)
    denominator = ONE + product(g_wide, g_wide + damping)
    return Stage(g=g, a1=int32(divided(1 << (TABLE_BITS + COEFFICIENT_BITS), denominator)))


def g_wide_of(stage):
    return stage.g << (TABLE_BITS - FINE_G_BITS) if stage.fine else _widened(stage.g)


def damping_of(stage):
    g_wide = g_wide_of(stage)
    if not stage.fine:
        return _effective_damping(stage.a1, g_wide)
    complement = stage.a1 << (TABLE_BITS - FINE_C_BITS)
    spread = divided(complement << TABLE_BITS, ONE - complement)
    return divided(spread << TABLE_BITS, g_wide) - g_wide


def design(band, rate):
    sine, cosine = _half_angle(band.frequency_hz, rate)
    tangent = divided(sine << TABLE_BITS, cosine)
    gain = clamped_gain(band.gain_tenths)
    if band.filter == eq_text.HIGH_SHELF:
        gain = -gain
    level = amplitude(clamped_gain(gain) * TENTHS)
    q_hundredths = clamped_q(band.q_hundredths)
    shelf = band.filter != eq_text.PEAK
    width = divided(ONE * q_hundredths, HUNDREDTHS) if shelf else divided(level * q_hundredths, HUNDREDTHS)
    damping = divided(ONE << TABLE_BITS, width)
    if shelf:
        tangent = multiply(tangent, amplitude(truncated(-gain * TENTHS, 2)))
    stage = _core_of(tangent, damping)
    squared_minus_one = product(level, level) - ONE
    effective = damping_of(stage)
    if not shelf:
        stage.m1 = _coefficient_m1(product(effective, squared_minus_one))
        return stage
    stage.m1 = _coefficient_m1(product(effective, level - ONE))
    stage.m2 = _coefficient(squared_minus_one)
    return stage


def stage_of(g, k, m1, m2):
    stage = _core_of(g, k)
    stage.m1 = _coefficient_m1(m1)
    stage.m2 = _coefficient(m2)
    return stage


def tangent(frequency, rate):
    sine, cosine = _half_angle(frequency, rate)
    return divided(sine << RESPONSE_BITS, cosine)


def shape(stage):
    g = g_wide_of(stage)
    return Shape(g_wide=g, g=g >> (TABLE_BITS - RESPONSE_BITS),
                 k=damping_of(stage) >> (TABLE_BITS - RESPONSE_BITS),
                 m1=stage.m1 * (1 << (RESPONSE_BITS - M1_BITS)),
                 m2=stage.m2 * (1 << (RESPONSE_BITS - COEFFICIENT_BITS)))


def _divided_array(numerator, denominator):
    total = numerator + np.trunc(denominator / 2).astype(np.int64)
    quotient = np.abs(total) // np.abs(denominator)
    return np.where((total >= 0) == (denominator > 0), quotient, -quotient)


def times(first, second):
    larger = np.maximum(first, second)
    smaller = np.minimum(first, second)
    larger = np.minimum(larger, R_CAP)
    result = np.where(larger < R_WIDE, (larger * smaller) >> RESPONSE_BITS,
                      (larger >> RESPONSE_BITS) * smaller)
    return np.minimum(result, R_CAP)


def response_squared(stage_shape, tangents):
    tangents = np.asarray(tangents, dtype=np.int64)
    g_wide = np.int64(stage_shape.g_wide)
    omega = _divided_array(tangents << TABLE_BITS, np.full_like(tangents, g_wide))
    w = (omega * omega) >> RESPONSE_BITS
    real_top = R_ONE + stage_shape.m2 - w
    imaginary_top = ((stage_shape.k + stage_shape.m1) * omega) >> RESPONSE_BITS
    real_bottom = R_ONE - w
    imaginary_bottom = (stage_shape.k * omega) >> RESPONSE_BITS
    top = real_top * real_top + imaginary_top * imaginary_top
    bottom = (real_bottom * real_bottom + imaginary_bottom * imaginary_bottom) >> RESPONSE_BITS
    bottom = np.where(bottom > 0, bottom, 1)
    quotient = np.abs(top) // bottom
    quotient = np.where(top >= 0, quotient, -quotient)
    low = times(np.int64(R_ONE + stage_shape.m2), np.int64(R_ONE + stage_shape.m2))
    return np.where(omega < OMEGA_MIN, low, np.where(omega > OMEGA_MAX, R_ONE, quotient))

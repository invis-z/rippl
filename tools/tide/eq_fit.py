from dataclasses import dataclass, field

import numpy as np

from tide import eq_band, eq_text

RATES = (8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000)
MAX_RATE = 96000
MAX_POINTS = 256
LOWEST_HZ = 20
HIGHEST_HZ = 20000
TOP_PERCENT = 44
PULL_HIGH_PERCENT = 42
PULL_LOW_PERCENT = 25
FREE_PERCENT = 10
PERCENT = 100
TENTHS_TO_HUNDREDTHS = 10
HUNDREDTHS_LOW = -2400
HUNDREDTHS_HIGH = 1200
WORST_UNKNOWN = 0xFFFF
SPACED_STARTS = 8
PLAYER_SPACED_STARTS = 2
ROUNDS = 1000
STALL_ROUNDS = 10
PLAYER_ROUNDS = 200
PLAYER_STALL_ROUNDS = 5
POLISH_PASSES = 8
POLISH_REACH = 2
KEPT_STARTS = 3

F32 = np.float32
POINT_STEP = F32(1.0293022366)
Q16 = F32(65536.0)
Q30 = F32(1073741824.0)
GOOD_ENOUGH = 0.0005
STALL_SHARE = 0.001
PLAYER_GOOD_ENOUGH = 0.0023
PLAYER_STALL_SHARE = 0.01
FROZEN = 0.0002
LAMBDA_START = 0.001
LAMBDA_DOWN = 3.0
LAMBDA_UP = 4.0
LAMBDA_LIMIT = 1.0e10
LAMBDA_FLOOR = 1.0e-9
DIAGONAL_FLOOR = 1.0e-15
G_LOW = 1.0 / 16384.0
G_HIGH = 65535.0 / 16384.0
K_LOW = 0.02
K_HIGH = 20.0
SPAN_HIGH = 100.0
M1_LIMIT = 31.9
M2_LOW = -0.98
M2_HIGH = 3.99


@dataclass
class Fit:
    rate: int
    target_rate: int
    worst_hundredths: int
    stages: list = field(default_factory=list)


@dataclass
class Grid:
    rate_tangent: np.ndarray
    target_tangent: np.ndarray
    fixed_target: np.ndarray
    tangent: np.ndarray
    target: np.ndarray


@dataclass
class Free:
    g: float
    k: float
    m1: float
    m2: float
    shelf: bool
    band: int
    free: bool


def top_of(rate, target):
    lower = min(rate, target)
    return min(lower // PERCENT * TOP_PERCENT, HIGHEST_HZ)


def needed(curve, rate):
    target = curve.target()
    if rate == 0 or rate == target or rate > MAX_RATE:
        return False
    return any(eq_band.audible(band, target) for band in curve.bands[:eq_text.MAX_BANDS])


def _fixed_product(shapes, tangents):
    product = np.full(len(tangents), eq_band.R_ONE, dtype=np.int64)
    for stage_shape in shapes:
        product = eq_band.times(product, eq_band.response_squared(stage_shape, tangents))
    return product


def prepare(curve, rate):
    target = curve.target()
    top = F32(top_of(rate, target))
    hertz = []
    frequency = F32(LOWEST_HZ)
    while frequency <= top and len(hertz) < MAX_POINTS:
        hertz.append(int(F32(frequency + F32(0.5))))
        frequency = F32(frequency * POINT_STEP)
    rate_tangent = np.array([eq_band.tangent(value, rate) for value in hertz], dtype=np.int64)
    target_tangent = np.array([eq_band.tangent(value, target) for value in hertz], dtype=np.int64)
    shapes = [eq_band.shape(eq_band.design(band, target))
              for band in curve.bands[:eq_text.MAX_BANDS] if eq_band.audible(band, target)]
    if not shapes or not hertz:
        return None
    fixed_target = _fixed_product(shapes, target_tangent)
    fixed_target = np.where(fixed_target > 0, fixed_target, 1)
    return Grid(rate_tangent=rate_tangent, target_tangent=target_tangent, fixed_target=fixed_target,
                tangent=rate_tangent.astype(np.float32) / Q16,
                target=fixed_target.astype(np.float32) / Q16)


def power_of_hundredths(hundredths):
    level = F32(eq_band.amplitude(hundredths)) / Q30
    squared = F32(level * level)
    return F32(squared * squared)


def _below(ratio):
    low, high = HUNDREDTHS_LOW, HUNDREDTHS_HIGH
    while high - low > 1:
        middle = eq_band.truncated(low + high, 2)
        if power_of_hundredths(middle) <= ratio:
            low = middle
        else:
            high = middle
    return low


def _above(ratio):
    low, high = HUNDREDTHS_LOW, HUNDREDTHS_HIGH
    while high - low > 1:
        middle = eq_band.truncated(low + high, 2)
        if power_of_hundredths(middle) >= ratio:
            high = middle
        else:
            low = middle
    return high


def worst_hundredths(low, high):
    return max(max(-_below(low), _above(high)), 0) & 0xFFFF


def spread(grid, stages, level=F32(1.0)):
    shapes = [eq_band.shape(stage) for stage in stages]
    ratio = (_fixed_product(shapes, grid.rate_tangent).astype(np.float32)
             / grid.fixed_target.astype(np.float32)) * F32(level)
    return min(F32(1.0), F32(ratio.min())), max(F32(1.0), F32(ratio.max()))


def _lift(curve, rate):
    return sum(eq_band.clamped_gain(band.gain_tenths) for band in curve.bands[:eq_text.MAX_BANDS]
               if band.filter == eq_text.HIGH_SHELF and eq_band.audible(band, rate))


def today_hundredths(curve, rate, grid):
    stages = [eq_band.design(band, rate) for band in curve.bands[:eq_text.MAX_BANDS]
              if eq_band.audible(band, rate)]
    level = power_of_hundredths((_lift(curve, rate) - _lift(curve, curve.target()))
                                * TENTHS_TO_HUNDREDTHS)
    return worst_hundredths(*spread(grid, stages, level))


def usable(stage):
    g_limit = eq_band.FINE_G_LIMIT - 1 if stage.fine else eq_band.COEFFICIENT_MAX
    a1_limit = eq_band.FINE_C_LIMIT if stage.fine else (1 << eq_band.COEFFICIENT_BITS) - 1
    return (0 < stage.g <= g_limit and 0 < stage.a1 < a1_limit
            and -eq_band.M1_MAX <= stage.m1 <= eq_band.M1_MAX
            and -eq_band.COEFFICIENT_MAX <= stage.m2 <= eq_band.COEFFICIENT_MAX
            and eq_band.shape(stage).k > 0)


def _free_of(designed, band, index):
    stage_shape = eq_band.shape(designed)
    return Free(g=stage_shape.g_wide / float(Q30), k=stage_shape.k / float(Q16),
                m1=stage_shape.m1 / float(Q16), m2=stage_shape.m2 / float(Q16),
                shelf=band.filter != eq_text.PEAK, band=index, free=True)


def _band_matches(grid, band, rate, target):
    here = eq_band.response_squared(eq_band.shape(eq_band.design(band, rate)), grid.rate_tangent)
    there = eq_band.response_squared(eq_band.shape(eq_band.design(band, target)), grid.target_tangent)
    return bool(np.all(np.abs(here / there - 1.0) < FROZEN))


def _frequency_of(g_wide, rate):
    low, high = 1, rate // PERCENT * TOP_PERCENT
    wanted = g_wide >> (eq_band.TABLE_BITS - eq_band.RESPONSE_BITS)
    while high - low > 1:
        middle = (low + high) // 2
        if eq_band.tangent(middle, rate) <= wanted:
            low = middle
        else:
            high = middle
    return low


def _seeded(seed, index, rate, stage):
    for seeded in seed.stages:
        if seeded.band != index:
            continue
        stage_shape = eq_band.shape(seeded)
        hertz = min(_frequency_of(stage_shape.g_wide, seed.rate), rate // PERCENT * PULL_HIGH_PERCENT)
        stage.g = min(max(eq_band.tangent(hertz, rate) / float(Q16), G_LOW), G_HIGH)
        stage.k = min(max(stage_shape.k / float(Q16), K_LOW), K_HIGH)
        stage.m1 = min(max(stage_shape.m1 / float(Q16), -M1_LIMIT), M1_LIMIT)
        if stage.shelf:
            stage.m2 = min(max(stage_shape.m2 / float(Q16), M2_LOW), M2_HIGH)
        return


def _pulled(curve, rate, target):
    return any(eq_band.audible(band, target) and not eq_band.audible(band, rate)
               for band in curve.bands[:eq_text.MAX_BANDS])


def _start(curve, rate, grid, percent, seed):
    target = curve.target()
    stages = []
    designed = []
    for index, band in enumerate(curve.bands[:eq_text.MAX_BANDS]):
        if not eq_band.audible(band, target):
            continue
        pulled = not eq_band.audible(band, rate)
        placed = band
        if pulled:
            placed = eq_text.Band(rate // PERCENT * percent, band.gain_tenths, band.q_hundredths,
                                  band.filter)
        stage = eq_band.design(placed, rate)
        stage.band = index
        free = _free_of(stage, band, index)
        low = band.frequency_hz * PERCENT < min(rate, target) * FREE_PERCENT
        free.free = pulled or (not low and not _band_matches(grid, band, rate, target))
        if seed is not None and free.free:
            _seeded(seed, index, rate, free)
        designed.append(stage)
        stages.append(free)
    return stages, designed


def _power(stage, tangent, slopes):
    omega = tangent / stage.g
    squared = omega * omega
    a = 1.0 + stage.m2 - squared
    b = stage.k + stage.m1
    c = 1.0 - squared
    top = a * a + b * b * squared
    bottom = c * c + stage.k * stage.k * squared
    if slopes:
        top_omega = 2.0 * omega * (b * b - 2.0 * a)
        bottom_omega = 2.0 * omega * (stage.k * stage.k - 2.0 * c)
        columns = [(top_omega / top - bottom_omega / bottom) * (-omega / stage.g),
                   2.0 * squared * (b / top - stage.k / bottom),
                   2.0 * squared * b / top]
        if stage.shelf:
            columns.append(2.0 * a / top)
        return top / bottom, columns
    return top / bottom, None


def _ratio(stages, tangent, frozen, slopes=False):
    ratio = frozen.copy()
    columns = []
    for stage in stages:
        if not stage.free:
            continue
        power, stage_columns = _power(stage, tangent, slopes)
        ratio *= power
        if slopes:
            columns.extend(stage_columns)
    return ratio, columns


def _cost(error):
    return float(np.dot(error, error)), float(np.max(np.abs(error)))


def _moved(stages, step):
    moved = []
    used = 0
    for stage in stages:
        if not stage.free:
            moved.append(stage)
            continue
        g = min(max(stage.g + step[used], G_LOW), G_HIGH)
        k = min(max(stage.k + step[used + 1], K_LOW), K_HIGH)
        if g * (g + k) > SPAN_HIGH:
            k = min(max(SPAN_HIGH / g - g, K_LOW), K_HIGH)
        m1 = min(max(stage.m1 + step[used + 2], -M1_LIMIT), M1_LIMIT)
        m2 = stage.m2
        used += 3
        if stage.shelf:
            m2 = min(max(stage.m2 + step[used], M2_LOW), M2_HIGH)
            used += 1
        moved.append(Free(g, k, m1, m2, stage.shelf, stage.band, True))
    return moved


@dataclass(frozen=True)
class Plan:
    rounds: int
    good_enough: float
    stall_rounds: int
    stall_share: float


LONG = Plan(ROUNDS, GOOD_ENOUGH, STALL_ROUNDS, STALL_SHARE)
PLAYER = Plan(PLAYER_ROUNDS, PLAYER_GOOD_ENOUGH, PLAYER_STALL_ROUNDS, PLAYER_STALL_SHARE)


def _refine(stages, grid, plan=LONG):
    tangent = grid.tangent.astype(np.float64)
    frozen = np.ones(len(tangent))
    for stage in stages:
        if not stage.free:
            frozen *= _power(stage, tangent, False)[0]
    frozen /= grid.target.astype(np.float64)
    ratio, _ = _ratio(stages, tangent, frozen)
    cost, worst = _cost(ratio - 1.0)
    if not any(stage.free for stage in stages):
        return stages, cost
    lam = LAMBDA_START
    earlier = cost
    for round_number in range(1, plan.rounds + 1):
        if worst < plan.good_enough:
            break
        ratio, columns = _ratio(stages, tangent, frozen, slopes=True)
        jacobian = np.stack(columns, axis=1) * ratio[:, None]
        error = ratio - 1.0
        normal = jacobian.T @ jacobian
        gradient = jacobian.T @ error
        improved = False
        while lam < LAMBDA_LIMIT:
            damped = normal + np.diag(np.diag(normal) * lam + DIAGONAL_FLOOR)
            try:
                step = np.linalg.solve(damped, -gradient)
            except np.linalg.LinAlgError:
                lam *= LAMBDA_UP
                continue
            trial = _moved(stages, step)
            trial_ratio, _ = _ratio(trial, tangent, frozen)
            trial_cost, trial_worst = _cost(trial_ratio - 1.0)
            if trial_cost < cost:
                stages, cost, worst = trial, trial_cost, trial_worst
                lam = max(lam / LAMBDA_DOWN, LAMBDA_FLOOR)
                improved = True
                break
            lam *= LAMBDA_UP
        if not improved:
            break
        if round_number % plan.stall_rounds == 0:
            if earlier - cost < earlier * plan.stall_share:
                break
            earlier = cost
    return stages, cost


def _quantised(stage):
    result = eq_band.stage_of(int(stage.g * float(Q30)), int(stage.k * float(Q30)),
                              int(stage.m1 * float(Q30)), int(stage.m2 * float(Q30)) if stage.shelf else 0)
    result.band = stage.band
    return result


def _fitness(ratio):
    error = ratio - 1.0
    return float(np.max(np.abs(error))), float(np.dot(error, error))


def _response(grid, stage):
    return (eq_band.response_squared(eq_band.shape(stage), grid.rate_tangent).astype(np.float64)
            / float(Q16))


def _polish(grid, candidates, shelves):
    responses = [_response(grid, stage) for stage in candidates]
    ratio = np.prod(responses, axis=0) / grid.target.astype(np.float64)
    current = _fitness(ratio)
    for _ in range(POLISH_PASSES):
        changed = False
        for index, stage in enumerate(candidates):
            names = ("g", "a1", "m1", "m2") if shelves[index] else ("g", "a1", "m1")
            for name in names:
                for delta in range(-POLISH_REACH, POLISH_REACH + 1):
                    if delta == 0:
                        continue
                    nudged = eq_band.Stage(stage.g, stage.a1, stage.m1, stage.m2, stage.band, stage.fine)
                    setattr(nudged, name, getattr(nudged, name) + delta)
                    if not usable(nudged):
                        continue
                    trial = _response(grid, nudged)
                    trial_ratio = ratio / responses[index] * trial
                    fitness = _fitness(trial_ratio)
                    if fitness < current:
                        ratio, responses[index] = trial_ratio, trial
                        candidates[index] = stage = nudged
                        current = fitness
                        changed = True
        if not changed:
            break
    return candidates


def _percents(pulled):
    if not pulled:
        return [PULL_HIGH_PERCENT]
    step = (PULL_HIGH_PERCENT - PULL_LOW_PERCENT) / SPACED_STARTS
    return [PULL_HIGH_PERCENT - round(start * step) for start in range(SPACED_STARTS + 1)]


def _player_percents(pulled):
    if not pulled:
        return [PULL_HIGH_PERCENT]
    return [PULL_HIGH_PERCENT - start * (PULL_HIGH_PERCENT - PULL_LOW_PERCENT) // PLAYER_SPACED_STARTS
            for start in range(PLAYER_SPACED_STARTS + 1)]


def fit(curve, rate, seed=None):
    if not needed(curve, rate):
        return None, WORST_UNKNOWN
    grid = prepare(curve, rate)
    if grid is None:
        return None, WORST_UNKNOWN
    target = curve.target()
    today = today_hundredths(curve, rate, grid)
    pulled = _pulled(curve, rate, target)
    seeds = [None]
    if pulled and seed is not None and seed.target_rate == target and seed.stages:
        seeds.append(seed)
    refined = []
    for percent in _percents(pulled):
        for one in seeds:
            stages, designed = _start(curve, rate, grid, percent, one)
            stages, cost = _refine(stages, grid)
            refined.append((cost, stages, designed))
    refined.sort(key=lambda entry: entry[0])
    chosen = refined[:KEPT_STARTS]
    for percent in _player_percents(pulled):
        for one in seeds if percent == PULL_HIGH_PERCENT else [None]:
            stages, designed = _start(curve, rate, grid, percent, one)
            stages, cost = _refine(stages, grid, PLAYER)
            chosen.append((cost, stages, designed))
    best = None
    for _, stages, designed in chosen:
        candidates = [_quantised(stage) if stage.free else designed[index]
                      for index, stage in enumerate(stages)]
        candidates = _polish(grid, candidates, [stage.shelf for stage in stages])
        worst = worst_hundredths(*spread(grid, candidates))
        if best is None or worst < best.worst_hundredths:
            best = Fit(rate=rate, target_rate=target, worst_hundredths=worst, stages=candidates)
    if best is None or best.worst_hundredths >= today:
        return None, today
    return best, today


def fit_all(curve):
    fits = []
    seed = None
    for rate in reversed(RATES):
        found, _ = fit(curve, rate, seed)
        if found is not None:
            fits.append(found)
            seed = found
    return fits

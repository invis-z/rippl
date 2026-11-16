#include "dsp/equalizer_fit.h"

#include <stddef.h>

#include "equalizer_band.h"

enum {
    POLISH_PASSES = 4,
    FIT_STRIDE = 1,
    FREE_PERCENT = 10,
    POLISH_REACH = 2,
    COEFFICIENTS = 4,
    MAX_PARAMETERS = 4 * EQUALIZER_MAX_BANDS,
    MAX_POINTS = 256,
    PEAK_PARAMETERS = 3,
    SHELF_PARAMETERS = 4,
    LOWEST_HZ = 20,
    HIGHEST_HZ = 20000,
    TOP_PERCENT = 44,
    PULL_HIGH_PERCENT = 42,
    PULL_LOW_PERCENT = 25,
    PERCENT = 100,
    EARLY_ROUND = 25,
    EARLY_FACTOR = 4,
    STALL_ROUNDS = 5,
    TENTHS_TO_HUNDREDTHS = 10,
    HUNDREDTHS_LOW = -2400,
    HUNDREDTHS_HIGH = 1200,
    WORST_UNKNOWN = 0xFFFF
};

const uint32_t EQUALIZER_FIT_RATE_LIST[EQUALIZER_FIT_RATES] = {
    8000, 11025, 12000, 16000, 22050, 24000, 32000, 44100, 48000, 88200, 96000};

static const float POINT_STEP = 1.0293022366f;
static const float Q16 = 65536.0f;
static const float Q30 = 1073741824.0f;
static const float GOOD_ENOUGH = 0.0023f;
static const float STALL_SHARE = 0.01f;
static const float FROZEN = 0.0002f;
static const float LAMBDA_START = 0.001f;
static const float LAMBDA_DOWN = 3.0f;
static const float LAMBDA_UP = 4.0f;
static const float LAMBDA_LIMIT = 1.0e8f;
static const float LAMBDA_FLOOR = 1.0e-7f;
static const float DIAGONAL_FLOOR = 1.0e-12f;
static const float G_LOW = 1.0f / 16384.0f;
static const float G_HIGH = 65535.0f / 16384.0f;
static const float K_LOW = 0.02f;
static const float K_HIGH = 20.0f;
static const float SPAN_HIGH = 100.0f;
static const float M1_LIMIT = 31.9f;
static const float M2_LOW = -0.98f;
static const float M2_HIGH = 3.99f;

typedef struct Stage {
    float g;
    float inverse_g;
    float k;
    float m1;
    float m2;
    uint8_t shelf;
    uint8_t band;
    uint8_t free;
} Stage;

typedef struct Fitness {
    float cost;
    float worst;
} Fitness;

typedef struct Outcome {
    Fitness fitness;
    uint32_t rounds;
} Outcome;

typedef struct Spread {
    float low;
    float high;
} Spread;

typedef struct EqualizerFitWork {
    uint32_t points;
    uint32_t stage_count;
    uint32_t parameter_count;
    EqualizerFitBeat beat;
    uint32_t slope_evaluations;
    uint32_t plain_evaluations;
    uint32_t fixed_evaluations;
    float tangent[MAX_POINTS];
    float target[MAX_POINTS];
    float frozen[MAX_POINTS];
    int64_t rate_tangent[MAX_POINTS];
    int64_t target_tangent[MAX_POINTS];
    int64_t fixed_target[MAX_POINTS];
    float normal[MAX_PARAMETERS * MAX_PARAMETERS];
    float factor[MAX_PARAMETERS * MAX_PARAMETERS];
    float gradient[MAX_PARAMETERS];
    float step[MAX_PARAMETERS];
    Stage stages[EQUALIZER_MAX_BANDS];
    Stage trial[EQUALIZER_MAX_BANDS];
    EqualizerStage designed[EQUALIZER_MAX_BANDS];
    EqualizerStage candidate[EQUALIZER_MAX_BANDS];
    EqualizerStage chosen[EQUALIZER_MAX_BANDS];
    float response[EQUALIZER_MAX_BANDS][MAX_POINTS];
    float ratio[MAX_POINTS];
} Work;

uint32_t equalizer_fit_work_size(void)
{
    return (uint32_t)sizeof(Work);
}

static float bounded(float value, float low, float high)
{
    return value < low ? low : value > high ? high : value;
}

static float magnitude(float value)
{
    return value < 0.0f ? -value : value;
}

static uint32_t top_of(uint32_t rate, uint32_t target)
{
    uint32_t lower = rate < target ? rate : target;
    uint32_t top = lower / PERCENT * TOP_PERCENT;
    return top < HIGHEST_HZ ? top : HIGHEST_HZ;
}

static int lift_tenths(const EqualizerCurve *curve, uint32_t rate)
{
    int lift = 0;
    for (int index = 0; index < curve->band_count; index++) {
        EqualizerBand band = curve->bands[index];
        if (band.filter == EQUALIZER_HIGH_SHELF && equalizer_band_audible(band, rate)) {
            lift += equalizer_band_clamped_gain(band.gain_tenths);
        }
    }
    return lift;
}

static float power_of_hundredths(int hundredths)
{
    float amplitude = (float)equalizer_band_amplitude(hundredths) / Q30;
    float squared = amplitude * amplitude;
    return squared * squared;
}

static int hundredths_below(float ratio)
{
    int low = HUNDREDTHS_LOW;
    int high = HUNDREDTHS_HIGH;
    while (high - low > 1) {
        int middle = (low + high) / 2;
        if (power_of_hundredths(middle) <= ratio) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

static int hundredths_above(float ratio)
{
    int low = HUNDREDTHS_LOW;
    int high = HUNDREDTHS_HIGH;
    while (high - low > 1) {
        int middle = (low + high) / 2;
        if (power_of_hundredths(middle) >= ratio) {
            high = middle;
        } else {
            low = middle;
        }
    }
    return high;
}

static uint16_t worst_hundredths(Spread spread)
{
    int below = -hundredths_below(spread.low);
    int above = hundredths_above(spread.high);
    int worst = below > above ? below : above;
    return (uint16_t)(worst < 0 ? 0 : worst);
}

static uint32_t parameters_of(const Stage *stage)
{
    return stage->free ? (stage->shelf ? SHELF_PARAMETERS : PEAK_PARAMETERS) : 0u;
}

static void settle(Stage *stage)
{
    stage->inverse_g = 1.0f / stage->g;
}

static Stage stage_of(const EqualizerStage *designed, EqualizerBand band, int index)
{
    EqualizerShape shape = equalizer_band_shape(designed);
    Stage stage = {.g = (float)shape.g_wide / Q30,
                   .k = (float)shape.k / Q16,
                   .m1 = (float)shape.m1 / Q16,
                   .m2 = (float)shape.m2 / Q16,
                   .shelf = band.filter != EQUALIZER_PEAK ? 1u : 0u,
                   .band = (uint8_t)index,
                   .free = 1u};
    settle(&stage);
    return stage;
}

static EqualizerStage quantised(const Stage *stage)
{
    EqualizerStage result = equalizer_band_stage(
        (int64_t)(stage->g * Q30), (int64_t)(stage->k * Q30), (int64_t)(stage->m1 * Q30),
        stage->shelf ? (int64_t)(stage->m2 * Q30) : 0);
    result.band = stage->band;
    return result;
}

static float stage_power(const Stage *stage, float tangent, float *slopes)
{
    float omega = tangent * stage->inverse_g;
    float squared = omega * omega;
    float a = 1.0f + stage->m2 - squared;
    float b = stage->k + stage->m1;
    float c = 1.0f - squared;
    float top = a * a + b * b * squared;
    float bottom = c * c + stage->k * stage->k * squared;
    float over_bottom = 1.0f / bottom;
    if (slopes != NULL) {
        float over_top = 1.0f / top;
        float top_omega = 2.0f * omega * (b * b - 2.0f * a);
        float bottom_omega = 2.0f * omega * (stage->k * stage->k - 2.0f * c);
        slopes[0] =
            (top_omega * over_top - bottom_omega * over_bottom) * (-omega * stage->inverse_g);
        slopes[1] = 2.0f * squared * (b * over_top - stage->k * over_bottom);
        slopes[2] = 2.0f * squared * b * over_top;
        slopes[3] = 2.0f * a * over_top;
    }
    return top * over_bottom;
}

static int64_t fixed_product(const EqualizerShape *shapes, uint32_t count, int64_t tangent)
{
    int64_t product = EQUALIZER_BAND_R_ONE;
    for (uint32_t index = 0; index < count; index++) {
        product =
            equalizer_band_times(product, equalizer_band_response_squared(&shapes[index], tangent));
    }
    return product;
}

static Spread spread_of(Work *work, const EqualizerStage *stages, uint32_t count, float level)
{
    work->fixed_evaluations += work->points * count;
    EqualizerShape shapes[EQUALIZER_MAX_BANDS];
    for (uint32_t index = 0; index < count; index++) {
        shapes[index] = equalizer_band_shape(&stages[index]);
    }
    Spread spread = {1.0f, 1.0f};
    for (uint32_t point = 0; point < work->points; point++) {
        float ratio = (float)fixed_product(shapes, count, work->rate_tangent[point]) /
                      (float)work->fixed_target[point] * level;
        spread.low = ratio < spread.low ? ratio : spread.low;
        spread.high = ratio > spread.high ? ratio : spread.high;
    }
    return spread;
}

static bool prepare(Work *work, const EqualizerCurve *curve, uint32_t rate, uint32_t target)
{
    uint32_t top = top_of(rate, target);
    work->points = 0;
    for (float frequency = (float)LOWEST_HZ; frequency <= (float)top && work->points < MAX_POINTS;
         frequency *= POINT_STEP) {
        uint32_t hertz = (uint32_t)(frequency + 0.5f);
        work->rate_tangent[work->points] = equalizer_band_tangent(hertz, rate);
        work->target_tangent[work->points] = equalizer_band_tangent(hertz, target);
        work->tangent[work->points] = (float)work->rate_tangent[work->points] / Q16;
        work->points++;
    }
    EqualizerShape shapes[EQUALIZER_MAX_BANDS];
    uint32_t count = 0;
    for (int index = 0; index < curve->band_count && index < EQUALIZER_MAX_BANDS; index++) {
        if (equalizer_band_audible(curve->bands[index], target)) {
            EqualizerStage designed = equalizer_band_design(curve->bands[index], target);
            shapes[count++] = equalizer_band_shape(&designed);
        }
    }
    for (uint32_t point = 0; point < work->points; point++) {
        int64_t product = fixed_product(shapes, count, work->target_tangent[point]);
        work->fixed_target[point] = product > 0 ? product : 1;
        work->target[point] = (float)work->fixed_target[point] / Q16;
    }
    return count > 0 && work->points > 0;
}

static bool band_matches(const Work *work, EqualizerBand band, uint32_t rate, uint32_t target)
{
    EqualizerStage here = equalizer_band_design(band, rate);
    EqualizerStage there = equalizer_band_design(band, target);
    EqualizerShape here_shape = equalizer_band_shape(&here);
    EqualizerShape there_shape = equalizer_band_shape(&there);
    for (uint32_t point = 0; point < work->points; point++) {
        float ratio =
            (float)equalizer_band_response_squared(&here_shape, work->rate_tangent[point]) /
            (float)equalizer_band_response_squared(&there_shape, work->target_tangent[point]);
        if (magnitude(ratio - 1.0f) >= FROZEN) {
            return false;
        }
    }
    return true;
}

static uint32_t frequency_of(int64_t g_wide, uint32_t rate)
{
    uint32_t low = 1;
    uint32_t high = rate / PERCENT * TOP_PERCENT;
    int64_t wanted = g_wide >> (EQUALIZER_BAND_TABLE_BITS - EQUALIZER_BAND_RESPONSE_BITS);
    while (high - low > 1) {
        uint32_t middle = (low + high) / 2;
        if (equalizer_band_tangent(middle, rate) <= wanted) {
            low = middle;
        } else {
            high = middle;
        }
    }
    return low;
}

static bool seeded_stage(const EqualizerFit *seed, int index, uint32_t rate, Stage *stage)
{
    for (int at = 0; at < seed->stage_count; at++) {
        if (seed->stages[at].band != index) {
            continue;
        }
        EqualizerShape shape = equalizer_band_shape(&seed->stages[at]);
        uint32_t hertz = frequency_of(shape.g_wide, seed->rate);
        uint32_t highest = rate / PERCENT * PULL_HIGH_PERCENT;
        hertz = hertz < highest ? hertz : highest;
        stage->g = bounded((float)equalizer_band_tangent(hertz, rate) / Q16, G_LOW, G_HIGH);
        stage->k = bounded((float)shape.k / Q16, K_LOW, K_HIGH);
        stage->m1 = bounded((float)shape.m1 / Q16, -M1_LIMIT, M1_LIMIT);
        if (stage->shelf) {
            stage->m2 = bounded((float)shape.m2 / Q16, M2_LOW, M2_HIGH);
        }
        settle(stage);
        return true;
    }
    return false;
}

static void start_stages(Work *work, const EqualizerCurve *curve, uint32_t rate, uint32_t target,
                         uint32_t start, const EqualizerFit *seed)
{
    work->stage_count = 0;
    work->parameter_count = 0;
    for (int index = 0; index < curve->band_count && index < EQUALIZER_MAX_BANDS; index++) {
        EqualizerBand band = curve->bands[index];
        if (!equalizer_band_audible(band, target)) {
            continue;
        }
        bool pulled = !equalizer_band_audible(band, rate);
        EqualizerBand placed = band;
        if (pulled) {
            uint32_t percent = PULL_HIGH_PERCENT;
            if (start > 0) {
                percent = PULL_HIGH_PERCENT - start * (PULL_HIGH_PERCENT - PULL_LOW_PERCENT) /
                                                  EQUALIZER_FIT_SPACED_STARTS;
            }
            placed.frequency_hz = (uint16_t)(rate / PERCENT * percent);
        }
        EqualizerStage designed = equalizer_band_design(placed, rate);
        designed.band = (uint8_t)index;
        Stage stage = stage_of(&designed, band, index);
        uint32_t lower = rate < target ? rate : target;
        bool low = (uint32_t)band.frequency_hz * PERCENT < lower * FREE_PERCENT;
        stage.free = pulled || (!low && !band_matches(work, band, rate, target)) ? 1u : 0u;
        if (seed != NULL && stage.free) {
            (void)seeded_stage(seed, index, rate, &stage);
        }
        work->designed[work->stage_count] = designed;
        work->stages[work->stage_count++] = stage;
        work->parameter_count += parameters_of(&stage);
    }
}

static void prepare_frozen(Work *work)
{
    for (uint32_t point = 0; point < work->points; point++) {
        float product = 1.0f;
        for (uint32_t index = 0; index < work->stage_count; index++) {
            if (!work->stages[index].free) {
                product *= stage_power(&work->stages[index], work->tangent[point], NULL);
            }
        }
        work->frozen[point] = product / work->target[point];
    }
}

static uint32_t frozen_count(const Work *work, const Stage *stages)
{
    uint32_t count = 0;
    for (uint32_t index = 0; index < work->stage_count; index++) {
        count += stages[index].free ? 0u : 1u;
    }
    return count;
}

static Fitness fitness_of(Work *work, const Stage *stages)
{
    Fitness fitness = {0.0f, 0.0f};
    work->plain_evaluations += work->points * (work->stage_count - frozen_count(work, stages));
    for (uint32_t point = 0; point < work->points; point += FIT_STRIDE) {
        float ratio = work->frozen[point];
        for (uint32_t index = 0; index < work->stage_count; index++) {
            if (stages[index].free) {
                ratio *= stage_power(&stages[index], work->tangent[point], NULL);
            }
        }
        float error = ratio - 1.0f;
        fitness.cost += error * error;
        fitness.worst = magnitude(error) > fitness.worst ? magnitude(error) : fitness.worst;
    }
    return fitness;
}

static void build_normal(Work *work)
{
    uint32_t count = work->parameter_count;
    for (uint32_t index = 0; index < count * count; index++) {
        work->normal[index] = 0.0f;
    }
    for (uint32_t index = 0; index < count; index++) {
        work->gradient[index] = 0.0f;
    }
    float row[MAX_PARAMETERS];
    work->slope_evaluations +=
        work->points * (work->stage_count - frozen_count(work, work->stages));
    for (uint32_t point = 0; point < work->points; point += FIT_STRIDE) {
        float ratio = work->frozen[point];
        uint32_t used = 0;
        for (uint32_t index = 0; index < work->stage_count; index++) {
            const Stage *stage = &work->stages[index];
            if (!stage->free) {
                continue;
            }
            float slopes[SHELF_PARAMETERS];
            ratio *= stage_power(stage, work->tangent[point], slopes);
            for (uint32_t parameter = 0; parameter < parameters_of(stage); parameter++) {
                row[used++] = slopes[parameter];
            }
        }
        float error = ratio - 1.0f;
        for (uint32_t first = 0; first < count; first++) {
            float scaled = row[first] * ratio;
            work->gradient[first] += scaled * error;
            for (uint32_t second = 0; second <= first; second++) {
                work->normal[first * count + second] += scaled * row[second] * ratio;
            }
        }
    }
}

static bool factorise(Work *work, float lambda)
{
    uint32_t count = work->parameter_count;
    float *factor = work->factor;
    for (uint32_t column = 0; column < count; column++) {
        float diagonal = work->normal[column * count + column];
        float pivot = diagonal + lambda * diagonal + DIAGONAL_FLOOR;
        for (uint32_t inner = 0; inner < column; inner++) {
            float value = factor[column * count + inner];
            pivot -= value * value * factor[inner * count + inner];
        }
        if (!(pivot > 0.0f)) {
            return false;
        }
        factor[column * count + column] = pivot;
        for (uint32_t row = column + 1; row < count; row++) {
            float value = work->normal[row * count + column];
            for (uint32_t inner = 0; inner < column; inner++) {
                value -= factor[row * count + inner] * factor[column * count + inner] *
                         factor[inner * count + inner];
            }
            factor[row * count + column] = value / pivot;
        }
    }
    return true;
}

static void solve(Work *work)
{
    uint32_t count = work->parameter_count;
    const float *factor = work->factor;
    for (uint32_t row = 0; row < count; row++) {
        float value = -work->gradient[row];
        for (uint32_t inner = 0; inner < row; inner++) {
            value -= factor[row * count + inner] * work->step[inner];
        }
        work->step[row] = value;
    }
    for (uint32_t row = 0; row < count; row++) {
        work->step[row] /= factor[row * count + row];
    }
    for (uint32_t row = count; row-- > 0;) {
        float value = work->step[row];
        for (uint32_t inner = row + 1; inner < count; inner++) {
            value -= factor[inner * count + row] * work->step[inner];
        }
        work->step[row] = value;
    }
}

static void take_step(Work *work)
{
    uint32_t used = 0;
    for (uint32_t index = 0; index < work->stage_count; index++) {
        work->trial[index] = work->stages[index];
        Stage *stage = &work->trial[index];
        if (!stage->free) {
            continue;
        }
        stage->g = bounded(stage->g + work->step[used++], G_LOW, G_HIGH);
        stage->k = bounded(stage->k + work->step[used++], K_LOW, K_HIGH);
        if (stage->g * (stage->g + stage->k) > SPAN_HIGH) {
            stage->k = bounded(SPAN_HIGH / stage->g - stage->g, K_LOW, K_HIGH);
        }
        if (stage->g * (stage->g + stage->k) > SPAN_HIGH) {
            stage->k = bounded(SPAN_HIGH / stage->g - stage->g, K_LOW, K_HIGH);
        }
        stage->m1 = bounded(stage->m1 + work->step[used++], -M1_LIMIT, M1_LIMIT);
        if (stage->shelf) {
            stage->m2 = bounded(stage->m2 + work->step[used++], M2_LOW, M2_HIGH);
        }
        settle(stage);
    }
}

static bool improve(Work *work, Outcome *outcome, float *lambda)
{
    build_normal(work);
    while (*lambda < LAMBDA_LIMIT) {
        if (factorise(work, *lambda)) {
            solve(work);
            take_step(work);
            Fitness trial = fitness_of(work, work->trial);
            if (trial.cost < outcome->fitness.cost) {
                for (uint32_t index = 0; index < work->stage_count; index++) {
                    work->stages[index] = work->trial[index];
                }
                outcome->fitness = trial;
                *lambda =
                    *lambda / LAMBDA_DOWN > LAMBDA_FLOOR ? *lambda / LAMBDA_DOWN : LAMBDA_FLOOR;
                return true;
            }
        }
        *lambda *= LAMBDA_UP;
    }
    return false;
}

static Outcome refine(Work *work, bool may_quit, float best_cost, EqualizerFitBeat beat)
{
    Outcome outcome = {fitness_of(work, work->stages), 0};
    float lambda = LAMBDA_START;
    float earlier = outcome.fitness.cost;
    while (outcome.rounds < EQUALIZER_FIT_ROUNDS && outcome.fitness.worst >= GOOD_ENOUGH) {
        outcome.rounds++;
        if (beat != NULL) {
            beat();
        }
        if (!improve(work, &outcome, &lambda)) {
            break;
        }
        if (outcome.rounds % STALL_ROUNDS == 0) {
            if (earlier - outcome.fitness.cost < earlier * STALL_SHARE) {
                break;
            }
            earlier = outcome.fitness.cost;
        }
        if (may_quit && outcome.rounds == EARLY_ROUND &&
            outcome.fitness.cost > best_cost * EARLY_FACTOR) {
            break;
        }
    }
    return outcome;
}

static void respond(Work *work, const EqualizerStage *stage, float *response)
{
    EqualizerShape shape = equalizer_band_shape(stage);
    work->fixed_evaluations += work->points;
    for (uint32_t point = 0; point < work->points; point++) {
        response[point] =
            (float)equalizer_band_response_squared(&shape, work->rate_tangent[point]) / Q16;
    }
}

static Fitness fixed_fitness(Work *work, uint32_t skip, const float *replacement)
{
    Fitness fitness = {0.0f, 0.0f};
    for (uint32_t point = 0; point < work->points; point++) {
        float ratio = work->ratio[point];
        if (replacement != NULL) {
            ratio = ratio / work->response[skip][point] * replacement[point];
        }
        float error = ratio - 1.0f;
        fitness.cost += error * error;
        fitness.worst = magnitude(error) > fitness.worst ? magnitude(error) : fitness.worst;
    }
    return fitness;
}

static int32_t *coefficient_of(EqualizerStage *stage, uint32_t which)
{
    return which == 0 ? &stage->g : which == 1 ? &stage->a1 : which == 2 ? &stage->m1 : &stage->m2;
}

static bool usable(const EqualizerStage *stage)
{
    int32_t g_limit =
        stage->fine ? EQUALIZER_BAND_FINE_G_LIMIT - 1 : EQUALIZER_BAND_COEFFICIENT_MAX;
    int32_t a1_limit =
        stage->fine ? EQUALIZER_BAND_FINE_C_LIMIT : (1 << EQUALIZER_BAND_COEFFICIENT_BITS) - 1;
    return stage->g > 0 && stage->g <= g_limit && stage->a1 > 0 && stage->a1 < a1_limit &&
           stage->m1 >= -EQUALIZER_BAND_M1_MAX && stage->m1 <= EQUALIZER_BAND_M1_MAX &&
           stage->m2 >= -EQUALIZER_BAND_COEFFICIENT_MAX &&
           stage->m2 <= EQUALIZER_BAND_COEFFICIENT_MAX && equalizer_band_shape(stage).k > 0;
}

static bool better(Fitness candidate, Fitness current)
{
    return candidate.worst < current.worst ||
           (candidate.worst <= current.worst && candidate.cost < current.cost);
}

static void polish(Work *work)
{
    for (uint32_t index = 0; index < work->stage_count; index++) {
        respond(work, &work->candidate[index], work->response[index]);
    }
    for (uint32_t point = 0; point < work->points; point++) {
        float product = 1.0f;
        for (uint32_t index = 0; index < work->stage_count; index++) {
            product *= work->response[index][point];
        }
        work->ratio[point] = product / work->target[point];
    }
    Fitness current = fixed_fitness(work, 0, NULL);
    float trial[MAX_POINTS];
    for (uint32_t pass = 0; pass < POLISH_PASSES; pass++) {
        if (work->beat != NULL) {
            work->beat();
        }
        bool changed = false;
        for (uint32_t index = 0; index < work->stage_count; index++) {
            uint32_t coefficients = work->stages[index].shelf ? COEFFICIENTS : COEFFICIENTS - 1;
            for (uint32_t which = 0; which < coefficients; which++) {
                for (int delta = -POLISH_REACH; delta <= POLISH_REACH; delta++) {
                    if (delta == 0) {
                        continue;
                    }
                    EqualizerStage nudged = work->candidate[index];
                    *coefficient_of(&nudged, which) += delta;
                    if (!usable(&nudged)) {
                        continue;
                    }
                    respond(work, &nudged, trial);
                    Fitness fitness = fixed_fitness(work, index, trial);
                    if (!better(fitness, current)) {
                        continue;
                    }
                    for (uint32_t point = 0; point < work->points; point++) {
                        work->ratio[point] =
                            work->ratio[point] / work->response[index][point] * trial[point];
                        work->response[index][point] = trial[point];
                    }
                    work->candidate[index] = nudged;
                    current = fitness;
                    changed = true;
                }
            }
        }
        if (!changed) {
            break;
        }
    }
}

static void quantise_all(Work *work)
{
    for (uint32_t index = 0; index < work->stage_count; index++) {
        work->candidate[index] =
            work->stages[index].free ? quantised(&work->stages[index]) : work->designed[index];
    }
}

static uint16_t today_hundredths(Work *work, const EqualizerCurve *curve, uint32_t rate,
                                 uint32_t target)
{
    uint32_t count = 0;
    for (int index = 0; index < curve->band_count && index < EQUALIZER_MAX_BANDS; index++) {
        if (equalizer_band_audible(curve->bands[index], rate)) {
            work->candidate[count++] = equalizer_band_design(curve->bands[index], rate);
        }
    }
    int lift = lift_tenths(curve, rate) - lift_tenths(curve, target);
    float level = power_of_hundredths(lift * TENTHS_TO_HUNDREDTHS);
    return worst_hundredths(spread_of(work, work->candidate, count, level));
}

static bool any_pulled(const EqualizerCurve *curve, uint32_t rate, uint32_t target)
{
    for (int index = 0; index < curve->band_count && index < EQUALIZER_MAX_BANDS; index++) {
        if (equalizer_band_audible(curve->bands[index], target) &&
            !equalizer_band_audible(curve->bands[index], rate)) {
            return true;
        }
    }
    return false;
}

bool equalizer_fit_needed(const EqualizerCurve *curve, uint32_t rate)
{
    uint32_t target = equalizer_target_rate(curve);
    if (rate == 0 || rate == target || rate > EQUALIZER_MAX_RATE) {
        return false;
    }
    for (int index = 0; index < curve->band_count && index < EQUALIZER_MAX_BANDS; index++) {
        if (equalizer_band_audible(curve->bands[index], target)) {
            return true;
        }
    }
    return false;
}

static bool seed_fits(const EqualizerFit *seed, uint32_t target)
{
    return seed != NULL && seed->stage_count > 0 && seed->stage_count <= EQUALIZER_MAX_BANDS &&
           seed->target_rate == target && seed->rate > 0;
}

static uint32_t start_count(bool pulled, bool seeded)
{
    return pulled ? 1u + (seeded ? 1u : 0u) + EQUALIZER_FIT_SPACED_STARTS : 1u;
}

uint32_t equalizer_fit_starts(const EqualizerCurve *curve, uint32_t rate, const EqualizerFit *seed)
{
    uint32_t target = equalizer_target_rate(curve);
    if (!equalizer_fit_needed(curve, rate)) {
        return 0;
    }
    bool pulled = any_pulled(curve, rate, target);
    return start_count(pulled, pulled && seed_fits(seed, target));
}

bool equalizer_fit_part(const EqualizerCurve *curve, uint32_t rate, void *memory, uint32_t size,
                        EqualizerFitPart part, EqualizerFit *candidate, EqualizerFitStats *stats)
{
    *stats =
        (EqualizerFitStats){.today_hundredths = WORST_UNKNOWN, .fit_hundredths = WORST_UNKNOWN};
    Work *work = memory;
    uint32_t target = equalizer_target_rate(curve);
    if (work == NULL || size < sizeof(Work) || ((uintptr_t)memory & (sizeof(int64_t) - 1)) != 0 ||
        part.parts == 0 || !equalizer_fit_needed(curve, rate) ||
        !prepare(work, curve, rate, target)) {
        return false;
    }
    work->beat = part.beat;
    work->slope_evaluations = 0;
    work->plain_evaluations = 0;
    work->fixed_evaluations = 0;
    stats->today_hundredths = today_hundredths(work, curve, rate, target);
    bool pulled = any_pulled(curve, rate, target);
    bool seeded = pulled && seed_fits(part.seed, target);
    uint32_t starts = start_count(pulled, seeded);
    float best_cost = 0.0f;
    uint32_t chosen_count = 0;
    for (uint32_t start = part.part; start < starts; start += part.parts) {
        bool from_seed = seeded && start == 1;
        uint32_t spaced = seeded && start > 1 ? start - 1 : start;
        start_stages(work, curve, rate, target, from_seed ? 0 : spaced,
                     from_seed ? part.seed : NULL);
        prepare_frozen(work);
        Outcome outcome = refine(work, chosen_count > 0, best_cost, part.beat);
        stats->rounds += outcome.rounds;
        stats->starts++;
        quantise_all(work);
        if (part.beat != NULL) {
            part.beat();
        }
        polish(work);
        uint16_t worst =
            worst_hundredths(spread_of(work, work->candidate, work->stage_count, 1.0f));
        if (worst < stats->fit_hundredths) {
            stats->fit_hundredths = worst;
            best_cost = outcome.fitness.cost;
            chosen_count = work->stage_count;
            for (uint32_t index = 0; index < chosen_count; index++) {
                work->chosen[index] = work->candidate[index];
            }
        }
    }
    stats->slope_evaluations = work->slope_evaluations;
    stats->plain_evaluations = work->plain_evaluations;
    stats->fixed_evaluations = work->fixed_evaluations;
    if (chosen_count == 0) {
        return false;
    }
    *candidate = (EqualizerFit){.rate = rate,
                                .target_rate = target,
                                .worst_hundredths = stats->fit_hundredths,
                                .stage_count = (uint8_t)chosen_count};
    for (uint32_t index = 0; index < chosen_count; index++) {
        candidate->stages[index] = work->chosen[index];
    }
    return true;
}

bool equalizer_fit(const EqualizerCurve *curve, uint32_t rate, void *memory, uint32_t size,
                   EqualizerFit *fit, EqualizerFitStats *stats, EqualizerFitBeat beat,
                   const EqualizerFit *seed)
{
    EqualizerFitPart whole = {.part = 0, .parts = 1, .beat = beat, .seed = seed};
    EqualizerFit candidate;
    if (!equalizer_fit_part(curve, rate, memory, size, whole, &candidate, stats) ||
        candidate.worst_hundredths >= stats->today_hundredths) {
        return false;
    }
    *fit = candidate;
    return true;
}

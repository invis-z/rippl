#include "check.h"
#include "dsp/equalizer.h"
#include "dsp/equalizer_fit.h"

#include <stdio.h>
#include <time.h>

enum {
    WORK_WORDS = 8192,
    REAL_PRESETS = 4,
    REAL_HUNDREDTHS = 20,
    ANY_HUNDREDTHS = 100
};

typedef struct Preset {
    const char *name;
    EqualizerCurve curve;
} Preset;

static const Preset PRESETS[] = {
    {"HIFIMAN Sundara",
     {10,
      0,
      0,
      0,
      {{105, 65, 70, EQUALIZER_LOW_SHELF},
       {127, -20, 29, EQUALIZER_PEAK},
       {2009, 65, 296, EQUALIZER_PEAK},
       {4085, -44, 119, EQUALIZER_PEAK},
       {7580, 23, 66, EQUALIZER_PEAK},
       {10000, -16, 70, EQUALIZER_HIGH_SHELF},
       {63, -12, 240, EQUALIZER_PEAK},
       {38, 9, 227, EQUALIZER_PEAK},
       {789, -16, 410, EQUALIZER_PEAK},
       {5821, 19, 600, EQUALIZER_PEAK}}}},
    {"Moondrop Blessing 2",
     {10,
      0,
      0,
      0,
      {{105, 33, 70, EQUALIZER_LOW_SHELF},
       {1788, -30, 66, EQUALIZER_PEAK},
       {820, 22, 120, EQUALIZER_PEAK},
       {183, -14, 98, EQUALIZER_PEAK},
       {4409, 24, 47, EQUALIZER_PEAK},
       {10000, -45, 70, EQUALIZER_HIGH_SHELF},
       {4129, 9, 328, EQUALIZER_PEAK},
       {2785, -8, 398, EQUALIZER_PEAK},
       {1939, 5, 363, EQUALIZER_PEAK},
       {1358, -4, 413, EQUALIZER_PEAK}}}},
    {"Sennheiser HD 650",
     {10,
      0,
      0,
      0,
      {{105, 64, 70, EQUALIZER_LOW_SHELF},
       {8800, 51, 142, EQUALIZER_PEAK},
       {118, -31, 50, EQUALIZER_PEAK},
       {37, 7, 396, EQUALIZER_PEAK},
       {3169, -17, 389, EQUALIZER_PEAK},
       {10000, -21, 70, EQUALIZER_HIGH_SHELF},
       {1227, -12, 253, EQUALIZER_PEAK},
       {2055, 12, 323, EQUALIZER_PEAK},
       {587, 4, 119, EQUALIZER_PEAK},
       {5332, -11, 575, EQUALIZER_PEAK}}}},
    {"Sony WH-1000XM4",
     {10,
      0,
      0,
      0,
      {{105, -42, 70, EQUALIZER_LOW_SHELF},
       {143, -52, 110, EQUALIZER_PEAK},
       {2289, 61, 157, EQUALIZER_PEAK},
       {56, 12, 119, EQUALIZER_PEAK},
       {5144, -32, 600, EQUALIZER_PEAK},
       {10000, -10, 70, EQUALIZER_HIGH_SHELF},
       {407, 17, 314, EQUALIZER_PEAK},
       {6715, 30, 599, EQUALIZER_PEAK},
       {1007, 10, 341, EQUALIZER_PEAK},
       {576, -12, 355, EQUALIZER_PEAK}}}},
    {"Made up 1",
     {10,
      0,
      0,
      0,
      {{114, 53, 70, EQUALIZER_LOW_SHELF},
       {14206, -49, 70, EQUALIZER_HIGH_SHELF},
       {834, -35, 327, EQUALIZER_PEAK},
       {417, 19, 93, EQUALIZER_PEAK},
       {4903, 66, 575, EQUALIZER_PEAK},
       {216, 50, 471, EQUALIZER_PEAK},
       {665, -30, 307, EQUALIZER_PEAK},
       {14984, 64, 274, EQUALIZER_PEAK},
       {18766, -4, 64, EQUALIZER_PEAK},
       {14188, -11, 228, EQUALIZER_PEAK}}}},
    {"Made up 2",
     {10,
      0,
      0,
      0,
      {{85, 63, 70, EQUALIZER_LOW_SHELF},
       {17638, -49, 70, EQUALIZER_HIGH_SHELF},
       {752, 42, 50, EQUALIZER_PEAK},
       {558, -9, 300, EQUALIZER_PEAK},
       {2076, -43, 524, EQUALIZER_PEAK},
       {5063, 64, 54, EQUALIZER_PEAK},
       {55, -76, 192, EQUALIZER_PEAK},
       {10198, 70, 129, EQUALIZER_PEAK},
       {17848, -45, 143, EQUALIZER_PEAK},
       {13498, -75, 87, EQUALIZER_PEAK}}}},
    {"Made up 3",
     {10,
      0,
      0,
      0,
      {{118, 74, 70, EQUALIZER_LOW_SHELF},
       {10566, -70, 70, EQUALIZER_HIGH_SHELF},
       {6863, -11, 133, EQUALIZER_PEAK},
       {3593, 36, 592, EQUALIZER_PEAK},
       {2335, 72, 193, EQUALIZER_PEAK},
       {222, -9, 97, EQUALIZER_PEAK},
       {1543, -74, 54, EQUALIZER_PEAK},
       {15229, -6, 110, EQUALIZER_PEAK},
       {14961, -19, 459, EQUALIZER_PEAK},
       {11160, 4, 201, EQUALIZER_PEAK}}}},
    {"Made up 4",
     {10,
      0,
      0,
      0,
      {{90, 30, 70, EQUALIZER_LOW_SHELF},
       {13700, -8, 70, EQUALIZER_HIGH_SHELF},
       {1754, 54, 163, EQUALIZER_PEAK},
       {46, 22, 73, EQUALIZER_PEAK},
       {33, 22, 432, EQUALIZER_PEAK},
       {6951, 4, 315, EQUALIZER_PEAK},
       {162, 27, 59, EQUALIZER_PEAK},
       {11764, 41, 217, EQUALIZER_PEAK},
       {19940, -32, 54, EQUALIZER_PEAK},
       {13854, 58, 162, EQUALIZER_PEAK}}}},
};

static const EqualizerCurve CHU_2 = {10,
                                     1,
                                     -14,
                                     0,
                                     {{21, -43, 230, EQUALIZER_PEAK},
                                      {37, -120, 50, EQUALIZER_PEAK},
                                      {43, 18, 150, EQUALIZER_PEAK},
                                      {170, -27, 110, EQUALIZER_PEAK},
                                      {480, 17, 110, EQUALIZER_PEAK},
                                      {1400, -10, 150, EQUALIZER_PEAK},
                                      {2900, 16, 190, EQUALIZER_PEAK},
                                      {4700, -15, 430, EQUALIZER_PEAK},
                                      {7900, -21, 500, EQUALIZER_PEAK},
                                      {12000, -62, 500, EQUALIZER_PEAK}}};

static int64_t work[WORK_WORDS];

static bool fit_of(const EqualizerCurve *curve, uint32_t rate, EqualizerFit *fit,
                   EqualizerFitStats *stats)
{
    return equalizer_fit(curve, rate, work, sizeof work, fit, stats, NULL, NULL);
}

static void the_work_area_is_big_enough(void)
{
    CHECK(equalizer_fit_work_size() <= sizeof work);
}

static void every_preset_fits_every_rate_closely(void)
{
    printf("%-22s %7s %8s %8s %7s %6s %8s\n", "preset", "rate", "today", "fit", "rounds", "starts",
           "ms");
    for (size_t preset = 0; preset < sizeof PRESETS / sizeof PRESETS[0]; preset++) {
        EqualizerFit seed = {0};
        for (uint32_t index = EQUALIZER_FIT_RATES; index-- > 0;) {
            uint32_t rate = EQUALIZER_FIT_RATE_LIST[index];
            const EqualizerCurve *curve = &PRESETS[preset].curve;
            if (!equalizer_fit_needed(curve, rate)) {
                continue;
            }
            EqualizerFit fit;
            EqualizerFitStats stats;
            clock_t started = clock();
            bool fitted = equalizer_fit(curve, rate, work, sizeof work, &fit, &stats, NULL, &seed);
            double milliseconds = (double)(clock() - started) * 1000.0 / CLOCKS_PER_SEC;
            printf("%-22s %7u %6.2f dB %6.2f dB %7u %6u %8.1f\n", PRESETS[preset].name,
                   (unsigned)rate, stats.today_hundredths / 100.0, stats.fit_hundredths / 100.0,
                   (unsigned)stats.rounds, (unsigned)stats.starts, milliseconds);
            uint16_t closest = fitted ? fit.worst_hundredths : stats.today_hundredths;
            CHECK(closest <= (preset < REAL_PRESETS ? REAL_HUNDREDTHS : ANY_HUNDREDTHS));
            if (fitted) {
                seed = fit;
                CHECK(fit.worst_hundredths < stats.today_hundredths);
                CHECK_EQUAL(rate, fit.rate);
                CHECK_EQUAL(EQUALIZER_TARGET_RATE, fit.target_rate);
            } else {
                CHECK(stats.today_hundredths <= stats.fit_hundredths);
            }
        }
    }
}

static void the_target_rate_itself_needs_no_fit(void)
{
    EqualizerCurve curve = PRESETS[0].curve;
    CHECK(!equalizer_fit_needed(&curve, 48000));
    CHECK(equalizer_fit_needed(&curve, 44100));
    curve.target_rate = 44100;
    CHECK(!equalizer_fit_needed(&curve, 44100));
    CHECK(equalizer_fit_needed(&curve, 48000));
    CHECK(!equalizer_fit_needed(&curve, 192000));
}

static void a_flat_curve_needs_no_fit(void)
{
    EqualizerCurve curve = {0};
    CHECK(!equalizer_fit_needed(&curve, 24000));
    EqualizerFit fit;
    EqualizerFitStats stats;
    CHECK(!fit_of(&curve, 24000, &fit, &stats));
}

static void too_little_work_space_is_refused(void)
{
    EqualizerFit fit;
    EqualizerFitStats stats;
    CHECK(!equalizer_fit(&PRESETS[4].curve, 24000, work, 64, &fit, &stats, NULL, NULL));
}

static void the_equalizer_runs_the_fit_at_its_rate_only(void)
{
    const EqualizerCurve *curve = &PRESETS[4].curve;
    EqualizerFit fit;
    EqualizerFitStats stats;
    CHECK(fit_of(curve, 24000, &fit, &stats));
    static Equalizer equalizer;
    equalizer_init(&equalizer);
    equalizer_set_fit(&equalizer, &fit);
    equalizer_set_rate(&equalizer, 24000);
    equalizer_set_curve(&equalizer, curve);
    CHECK(equalizer_fitted(&equalizer));
    CHECK_EQUAL(fit.stage_count, equalizer.current.stage_count);
    for (int index = 0; index < fit.stage_count; index++) {
        CHECK_EQUAL(fit.stages[index].g, equalizer.current.stages[index].g);
        CHECK_EQUAL(fit.stages[index].m1, equalizer.current.stages[index].m1);
    }
    equalizer_set_rate(&equalizer, 48000);
    CHECK(!equalizer_fitted(&equalizer));
    equalizer_set_fit(&equalizer, NULL);
    equalizer_set_rate(&equalizer, 24000);
    CHECK(!equalizer_fitted(&equalizer));
}

static void a_fitted_chain_keeps_a_steady_tone_level(void)
{
    const EqualizerCurve *curve = &PRESETS[2].curve;
    EqualizerFit fit;
    EqualizerFitStats stats;
    CHECK(fit_of(curve, 24000, &fit, &stats));
    static Equalizer equalizer;
    equalizer_init(&equalizer);
    equalizer_set_fit(&equalizer, &fit);
    equalizer_set_rate(&equalizer, 24000);
    equalizer_set_curve(&equalizer, curve);
    static int32_t frames[2 * 4800];
    for (int index = 0; index < 4800; index++) {
        int32_t value = (index / 12) % 2 == 0 ? 400000 : -400000;
        frames[2 * index] = value;
        frames[2 * index + 1] = value;
    }
    equalizer_process(&equalizer, frames, 4800);
    int32_t peak = 0;
    for (int index = 2400; index < 4800; index++) {
        int32_t value = frames[2 * index] < 0 ? -frames[2 * index] : frames[2 * index];
        peak = value > peak ? value : peak;
        CHECK_EQUAL(frames[2 * index], frames[2 * index + 1]);
    }
    CHECK(peak > 100000);
    CHECK(peak < 8388607);
}

static void low_bands_stay_close_at_high_rates(void)
{
    static const uint32_t RATES[] = {44100, 88200, 96000};
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        EqualizerFit fit;
        EqualizerFitStats stats;
        CHECK(fit_of(&CHU_2, RATES[index], &fit, &stats));
        CHECK(fit.worst_hundredths <= 10);
    }
}

static void low_bands_use_the_fine_coefficients(void)
{
    EqualizerCurve curve = {.band_count = 2};
    curve.bands[0] = (EqualizerBand){21, -43, 230, EQUALIZER_PEAK};
    curve.bands[1] = (EqualizerBand){1000, 30, 100, EQUALIZER_PEAK};
    static Equalizer equalizer;
    equalizer_init(&equalizer);
    equalizer_set_rate(&equalizer, 96000);
    equalizer_set_curve(&equalizer, &curve);
    CHECK_EQUAL(2, equalizer.current.stage_count);
    CHECK_EQUAL(1, equalizer.current.stages[0].fine);
    CHECK_EQUAL(0, equalizer.current.stages[1].fine);
}

static void splitting_the_starts_finds_as_good_a_fit(void)
{
    static const uint32_t RATES[] = {24000, 16000, 11025};
    for (size_t preset = 0; preset < sizeof PRESETS / sizeof PRESETS[0]; preset++) {
        const EqualizerCurve *curve = &PRESETS[preset].curve;
        for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
            uint32_t rate = RATES[index];
            if (equalizer_fit_starts(curve, rate, NULL) < 2) {
                continue;
            }
            EqualizerFit whole;
            EqualizerFitStats stats;
            EqualizerFitPart all = {.part = 0, .parts = 1};
            CHECK(equalizer_fit_part(curve, rate, work, sizeof work, all, &whole, &stats));
            uint32_t starts = stats.starts;
            EqualizerFit halves[2];
            uint32_t split_starts = 0;
            uint16_t best = 0xFFFF;
            for (uint32_t part = 0; part < 2; part++) {
                EqualizerFitPart half = {.part = part, .parts = 2};
                CHECK(equalizer_fit_part(curve, rate, work, sizeof work, half, &halves[part],
                                         &stats));
                split_starts += stats.starts;
                best = halves[part].worst_hundredths < best ? halves[part].worst_hundredths : best;
            }
            CHECK_EQUAL(starts, split_starts);
            CHECK(best <= whole.worst_hundredths + 2);
        }
    }
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_work_area_is_big_enough),
        TEST_CASE(every_preset_fits_every_rate_closely),
        TEST_CASE(the_target_rate_itself_needs_no_fit),
        TEST_CASE(a_flat_curve_needs_no_fit),
        TEST_CASE(too_little_work_space_is_refused),
        TEST_CASE(the_equalizer_runs_the_fit_at_its_rate_only),
        TEST_CASE(a_fitted_chain_keeps_a_steady_tone_level),
        TEST_CASE(low_bands_stay_close_at_high_rates),
        TEST_CASE(low_bands_use_the_fine_coefficients),
        TEST_CASE(splitting_the_starts_finds_as_good_a_fit),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}

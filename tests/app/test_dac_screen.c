#include <string.h>

#include "check.h"
#include "dac_screen.h"
#include "default_theme.h"
#include "fonts/fonts.h"
#include "gfx/text.h"
#include "hal/display.h"

enum {
    STATS_INSET = 6
};

static void the_rate_shows_khz_with_one_decimal_only_when_needed(void)
{
    static const uint32_t RATES[] = {44100, 48000, 88200, 96000, 176400, 192000, 352800, 384000};
    static const char *const TEXTS[] = {"44.1", "48", "88.2", "96", "176.4", "192", "352.8", "384"};
    for (size_t index = 0; index < sizeof RATES / sizeof RATES[0]; index++) {
        char text[DAC_SCREEN_RATE_CAPACITY];
        dac_screen_rate_text(RATES[index], text, sizeof text);
        CHECK(strcmp(TEXTS[index], text) == 0);
    }
}

static AppDacStats busy_stats(void)
{
    return (AppDacStats){99999, 99999,   99999,  999,   999,  999,  999,
                         999,   9999999, 999999, 99999, 9999, 9999, 99999};
}

static void landscape_stats_take_three_lines(void)
{
    AppDacStats stats = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY];
    CHECK_EQUAL(3, dac_screen_stats_lines(&stats, false, lines));
    CHECK(strcmp("mis001 drp002 rep003 rst004 sta005 stp006", lines[0]) == 0);
    CHECK(strcmp("fmt007 try008 ctl009 slt010 irq011", lines[1]) == 0);
    CHECK(strcmp("loss012/013 gap0014us", lines[2]) == 0);
}

static void portrait_stats_take_five_lines(void)
{
    AppDacStats stats = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14};
    char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY];
    CHECK_EQUAL(5, dac_screen_stats_lines(&stats, true, lines));
    CHECK(strcmp("mis001 drp002 rep003", lines[0]) == 0);
    CHECK(strcmp("slt010 irq011", lines[3]) == 0);
    CHECK(strcmp("loss012/013 gap0014us", lines[4]) == 0);
}

static void counts_wrap_with_a_capital_and_measures_stop_at_nines(void)
{
    AppDacStats stats = {999, 1000, 1234, 0, 0, 0, 0, 0, 0, 0, 0, 5000, 7, 123456};
    char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY];
    dac_screen_stats_lines(&stats, true, lines);
    CHECK(strcmp("mis999 Drp000 Rep234", lines[0]) == 0);
    CHECK(strcmp("loss999/007 gap9999us", lines[4]) == 0);
}

static void the_stats_fit_across_the_screen(void)
{
    AppDacStats stats = busy_stats();
    for (int portrait = 0; portrait < 2; portrait++) {
        int width = (portrait ? HAL_DISPLAY_SHORT_SIDE : HAL_DISPLAY_LONG_SIDE) - 2 * STATS_INSET;
        char lines[DAC_SCREEN_STATS_LINES][DAC_SCREEN_STATS_CAPACITY];
        int count = dac_screen_stats_lines(&stats, portrait != 0, lines);
        for (int index = 0; index < count; index++) {
            CHECK(text_width(&FONT_MONO_12, lines[index]) <= width);
        }
    }
}

static void the_stats_panel_sits_at_the_bottom(void)
{
    Theme landscape = default_theme_for(false);
    Rect area = dac_screen_stats_area(&landscape, 3);
    CHECK_EQUAL(HAL_DISPLAY_SHORT_SIDE, area.y + area.height);
    CHECK_EQUAL(HAL_DISPLAY_LONG_SIDE, area.width);
    Theme portrait = default_theme_for(true);
    area = dac_screen_stats_area(&portrait, 5);
    CHECK_EQUAL(HAL_DISPLAY_LONG_SIDE, area.y + area.height);
    CHECK_EQUAL(HAL_DISPLAY_SHORT_SIDE, area.width);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_rate_shows_khz_with_one_decimal_only_when_needed),
        TEST_CASE(landscape_stats_take_three_lines),
        TEST_CASE(portrait_stats_take_five_lines),
        TEST_CASE(counts_wrap_with_a_capital_and_measures_stop_at_nines),
        TEST_CASE(the_stats_fit_across_the_screen),
        TEST_CASE(the_stats_panel_sits_at_the_bottom),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}

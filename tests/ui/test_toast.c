#include <string.h>

#include "check.h"
#include "ui/marquee.h"
#include "ui/toast.h"
#include "ui_test_canvas.h"
#include "ui_test_theme.h"

enum {
    NOW_MS = 1000,
    TALL_WIDTH = 40,
    TALL_HEIGHT = 60
};

static Color tall_pixels[TALL_WIDTH * TALL_HEIGHT];

static Theme tall_theme(void)
{
    Theme theme = TEST_THEME;
    theme.metrics.screen_width = TALL_WIDTH;
    theme.metrics.screen_height = TALL_HEIGHT;
    theme.metrics.padding = 2;
    theme.marquee = (MarqueeTiming){.pause_ms = 1200, .speed_px_per_s = 40, .gap_px = 8};
    return theme;
}

static Canvas blank_tall_canvas(void)
{
    Rect bounds = {0, 0, TALL_WIDTH, TALL_HEIGHT};
    Canvas canvas = canvas_make(tall_pixels, bounds);
    canvas_fill_rect(&canvas, bounds, TEST_THEME.colors->background);
    return canvas;
}

static void a_new_toast_is_hidden(void)
{
    Toast toast = {0};
    CHECK(!toast.visible);
}

static void a_shown_toast_lasts_its_duration(void)
{
    Toast toast = {0};
    toast_show(&toast, "A", NOW_MS);
    toast_update(&toast, NOW_MS + TOAST_DURATION_MS - 1);
    CHECK(toast.visible);
    toast_update(&toast, NOW_MS + TOAST_DURATION_MS);
    CHECK(!toast.visible);
}

static void a_visible_toast_paints_its_box(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    Canvas canvas = ui_test_canvas();
    toast_draw(&toast, &canvas, &TEST_THEME);
    Rect area = toast_area(&toast, &TEST_THEME);
    CHECK(area.width > 0);
    CHECK(area.y + area.height <= TEST_SCREEN_HEIGHT);
    CHECK(ui_test_area_has_color(&canvas, area, TEST_THEME.colors->header_background));
    CHECK(ui_test_area_has_color(&canvas, area, TEST_THEME.colors->header_text));
}

static void a_hidden_toast_paints_nothing(void)
{
    Toast toast = {0};
    toast_show(&toast, "AB", NOW_MS);
    toast_update(&toast, NOW_MS + TOAST_DURATION_MS);
    Canvas canvas = ui_test_canvas();
    Rect screen = {0, 0, TEST_SCREEN_WIDTH, TEST_SCREEN_HEIGHT};
    toast_draw(&toast, &canvas, &TEST_THEME);
    CHECK(!ui_test_area_has_color(&canvas, screen, TEST_THEME.colors->header_background));
}

static void a_two_line_toast_wraps_and_stays_up_longer(void)
{
    Theme theme = tall_theme();
    Toast single = {0};
    toast_show(&single, "AB", NOW_MS);
    Rect one_line = toast_area(&single, &theme);
    Toast wrapped = {0};
    toast_show(&wrapped, "AAAA AAAA", NOW_MS);
    Rect two_lines = toast_area(&wrapped, &theme);
    CHECK(two_lines.height > one_line.height);
    Canvas canvas = blank_tall_canvas();
    toast_draw(&wrapped, &canvas, &theme);
    int mid = two_lines.y + two_lines.height / 2;
    Rect top_half = {two_lines.x, two_lines.y, two_lines.width, mid - two_lines.y};
    Rect bottom_half = {two_lines.x, mid, two_lines.width, two_lines.y + two_lines.height - mid};
    CHECK(ui_test_area_has_color(&canvas, top_half, theme.colors->header_text));
    CHECK(ui_test_area_has_color(&canvas, bottom_half, theme.colors->header_text));
    toast_update(&wrapped, NOW_MS + TOAST_WRAPPED_DURATION_MS - 1);
    CHECK(wrapped.visible);
    toast_update(&wrapped, NOW_MS + TOAST_WRAPPED_DURATION_MS);
    CHECK(!wrapped.visible);
}

static void a_toast_over_two_lines_rolls_on_one_line(void)
{
    Theme theme = tall_theme();
    Toast single = {0};
    toast_show(&single, "AB", NOW_MS);
    Rect one_line = toast_area(&single, &theme);
    const char *text = "ABAB ABAB ABAB ABAB ABAB ABAB ABAB ABAB";
    Toast rolling = {0};
    toast_show(&rolling, text, NOW_MS);
    Rect area = toast_area(&rolling, &theme);
    CHECK_EQUAL(one_line.height, area.height);
    Canvas first = blank_tall_canvas();
    marquee_frame_start();
    toast_draw(&rolling, &first, &theme);
    CHECK(marquee_frame_wait_ms() <= TOAST_MARQUEE_PAUSE_MS);
    Color before[TALL_WIDTH * TALL_HEIGHT];
    memcpy(before, tall_pixels, sizeof before);
    toast_update(&rolling, NOW_MS + TOAST_MARQUEE_PAUSE_MS - 1);
    Canvas paused = blank_tall_canvas();
    toast_draw(&rolling, &paused, &theme);
    CHECK_EQUAL(0, memcmp(before, tall_pixels, sizeof before));
    toast_update(&rolling, NOW_MS + TOAST_MARQUEE_PAUSE_MS + 200);
    Canvas moved = blank_tall_canvas();
    marquee_frame_start();
    toast_draw(&rolling, &moved, &theme);
    CHECK(memcmp(before, tall_pixels, sizeof before) != 0);
    CHECK(marquee_frame_wait_ms() > 0);
    CHECK(rolling.until_ms > NOW_MS + TOAST_WRAPPED_DURATION_MS);
    CHECK(rolling.until_ms <= NOW_MS + TOAST_MAX_DURATION_MS);
}

static void a_very_long_rolling_toast_caps_its_duration(void)
{
    Theme theme = tall_theme();
    Toast rolling = {0};
    toast_show(&rolling,
               "AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA "
               "AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA "
               "AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA AAAA",
               NOW_MS);
    Canvas canvas = blank_tall_canvas();
    toast_draw(&rolling, &canvas, &theme);
    CHECK_EQUAL(NOW_MS + TOAST_MAX_DURATION_MS, rolling.until_ms);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(a_new_toast_is_hidden),
        TEST_CASE(a_shown_toast_lasts_its_duration),
        TEST_CASE(a_visible_toast_paints_its_box),
        TEST_CASE(a_hidden_toast_paints_nothing),
        TEST_CASE(a_two_line_toast_wraps_and_stays_up_longer),
        TEST_CASE(a_toast_over_two_lines_rolls_on_one_line),
        TEST_CASE(a_very_long_rolling_toast_caps_its_duration),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}

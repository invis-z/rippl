#include "frame_overlay.h"

#include "core/string_builder.h"
#include "hal/display.h"
#include "ui/label.h"

enum {
    OVERLAY_WIDTH = 168,
    OVERLAY_LINE_HEIGHT = 18,
    OVERLAY_LINES = 4,
    OVERLAY_HEIGHT = OVERLAY_LINE_HEIGHT * OVERLAY_LINES,
    OVERLAY_MARGIN = 4,
    MICROSECONDS_PER_MILLISECOND = 1000,
    PERCENT = 100
};

static const Color OVERLAY_TEXT = COLOR_RGB(124, 252, 0);
static const Color OVERLAY_EDGE = COLOR_RGB(0, 0, 0);

Rect frame_overlay_area(const Theme *theme)
{
    int width = theme->metrics.screen_width;
    int height = theme->metrics.screen_height;
    int left = width - OVERLAY_WIDTH - OVERLAY_MARGIN;
    if (left < 0) {
        left = (width - OVERLAY_WIDTH) / 2;
    }
    (void)height;
    return (Rect){left, 0, OVERLAY_WIDTH, OVERLAY_HEIGHT};
}

Color frame_overlay_background(void)
{
    return COLOR_RGB(0, 0, 0);
}

static void append_milliseconds(StringBuilder *builder, const char *prefix, uint32_t microseconds)
{
    string_builder_append(builder, prefix);
    string_builder_append_unsigned(builder, (microseconds + MICROSECONDS_PER_MILLISECOND / 2) /
                                                MICROSECONDS_PER_MILLISECOND);
}

void frame_overlay_format(FrameBreakdown breakdown, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    append_milliseconds(&builder, "", breakdown.total_us);
    append_milliseconds(&builder, " u", breakdown.update_us);
    append_milliseconds(&builder, " d", breakdown.draw_us);
    append_milliseconds(&builder, " s", breakdown.send_us);
}

void frame_overlay_format_parts(const uint32_t *parts_us, char *text, size_t capacity)
{
    static const char *const PREFIXES[DRAW_PART_COUNT] = {"cv", " r", " t", " bg", " h", " a"};
    StringBuilder builder = string_builder_make(text, capacity);
    for (int part = 0; part < DRAW_PART_COUNT; part++) {
        append_milliseconds(&builder, PREFIXES[part], parts_us[part]);
    }
}

static uint32_t share_percent(uint32_t part, uint32_t whole)
{
    return whole == 0 ? 0 : (uint32_t)((uint64_t)part * PERCENT / whole);
}

void frame_overlay_format_audio(HalAudioStats stats, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append(&builder, "b");
    string_builder_append_unsigned(&builder, stats.buffered_ms);
    string_builder_append(&builder, " x");
    string_builder_append_unsigned(&builder, stats.underruns);
    string_builder_append(&builder, " o");
    string_builder_append_unsigned(&builder, stats.output_underruns);
    string_builder_append(&builder, " L");
    string_builder_append_unsigned(&builder, share_percent(stats.busy_us, stats.elapsed_us));
    string_builder_append(&builder, "% c");
    string_builder_append_unsigned(&builder, stats.core1_load);
    string_builder_append(&builder, "%");
}

static void append_average_and_longest(StringBuilder *builder, const char *prefix,
                                       uint32_t total_us, uint32_t blocks, uint32_t longest_us)
{
    append_milliseconds(builder, prefix, blocks == 0 ? 0 : total_us / blocks);
    append_milliseconds(builder, "/", longest_us);
}

void frame_overlay_format_block_times(HalAudioStats stats, char *text, size_t capacity)
{
    StringBuilder builder = string_builder_make(text, capacity);
    append_average_and_longest(&builder, "dec", stats.decode_total_us, stats.blocks,
                               stats.decode_max_us);
    append_average_and_longest(&builder, " rd", stats.read_total_us, stats.blocks,
                               stats.read_max_us);
}

static void draw_line(Canvas *canvas, const Theme *theme, Rect area, const char *text)
{
    static const int SHIFTS[4][2] = {{-1, 0}, {1, 0}, {0, -1}, {0, 1}};
    TextStyle style = {.font = theme->fonts.small, .color = OVERLAY_EDGE};
    Rect place = label_centered_area(style.font, area, text);
    for (int shift = 0; shift < 4; shift++) {
        Rect edge = {place.x + SHIFTS[shift][0], place.y + SHIFTS[shift][1], place.width,
                     place.height};
        label_draw(canvas, style, (Label){.area = edge, .text = text});
    }
    style.color = OVERLAY_TEXT;
    label_draw(canvas, style, (Label){.area = place, .text = text});
}

Color frame_overlay_text_color(void)
{
    return OVERLAY_TEXT;
}

void frame_overlay_draw(Canvas *canvas, const Theme *theme, FrameBreakdown breakdown,
                        HalAudioStats stats)
{
    Rect area = frame_overlay_area(theme);
    if (rect_is_empty(rect_intersection(area, canvas->clip))) {
        return;
    }
    char text[FRAME_OVERLAY_TEXT_CAPACITY];
    Rect line = {area.x, area.y, area.width, OVERLAY_LINE_HEIGHT};
    frame_overlay_format(breakdown, text, sizeof text);
    draw_line(canvas, theme, line, text);
    line.y += OVERLAY_LINE_HEIGHT;
    frame_overlay_format_audio(stats, text, sizeof text);
    draw_line(canvas, theme, line, text);
    line.y += OVERLAY_LINE_HEIGHT;
    frame_overlay_format_block_times(stats, text, sizeof text);
    draw_line(canvas, theme, line, text);
    line.y += OVERLAY_LINE_HEIGHT;
    uint32_t parts[DRAW_PART_COUNT];
    for (int part = 0; part < DRAW_PART_COUNT; part++) {
        parts[part] = draw_profile_average_us((DrawPart)part);
    }
    frame_overlay_format_parts(parts, text, sizeof text);
    draw_line(canvas, theme, line, text);
}

#include "frame_overlay.h"

#include "fonts/fonts.h"
#include "gfx/text.h"
#include "stat_text.h"

enum {
    OVERLAY_LEFT = 4,
    OVERLAY_PAD = 2,
    MICROSECONDS_PER_MILLISECOND = 1000,
    PERCENT = 100
};

static const Color OVERLAY_TEXT = COLOR_RGB(124, 252, 0);

static int line_count(const Theme *theme)
{
    return theme_is_portrait(theme) ? FRAME_OVERLAY_LINES : FRAME_OVERLAY_LINES / 2;
}

Rect frame_overlay_area(const Theme *theme)
{
    return (Rect){0, 0, theme->metrics.screen_width,
                  font_line_height(&FONT_MONO_12) * line_count(theme)};
}

Color frame_overlay_background(void)
{
    return COLOR_RGB(0, 0, 0);
}

Color frame_overlay_text_color(void)
{
    return OVERLAY_TEXT;
}

static uint32_t milliseconds(uint32_t microseconds)
{
    return (microseconds + MICROSECONDS_PER_MILLISECOND / 2) / MICROSECONDS_PER_MILLISECOND;
}

static uint32_t share_percent(uint32_t part, uint32_t whole)
{
    return whole == 0 ? 0 : (uint32_t)((uint64_t)part * PERCENT / whole);
}

static uint32_t average(uint32_t total_us, uint32_t blocks)
{
    return blocks == 0 ? 0 : milliseconds(total_us / blocks);
}

typedef struct LineWriter {
    char (*lines)[FRAME_OVERLAY_TEXT_CAPACITY];
    int line;
    StringBuilder builder;
    bool empty;
} LineWriter;

static void next_line(LineWriter *writer)
{
    writer->line++;
    writer->builder = string_builder_make(writer->lines[writer->line], FRAME_OVERLAY_TEXT_CAPACITY);
    writer->empty = true;
}

static void put(LineWriter *writer, const char *label, uint32_t value, uint32_t digits,
                StatKind kind)
{
    if (!writer->empty && label[0] != '/') {
        string_builder_append_char(&writer->builder, ' ');
    }
    stat_text_put(&writer->builder, label, value, digits, kind);
    writer->empty = false;
}

int frame_overlay_lines(FrameBreakdown breakdown, const uint32_t *parts_us, HalAudioStats stats,
                        bool portrait, char lines[FRAME_OVERLAY_LINES][FRAME_OVERLAY_TEXT_CAPACITY])
{
    static const char *const PARTS[DRAW_PART_COUNT] = {"cv", "r", "t", "bg", "h", "a"};
    LineWriter writer = {lines, -1, {0}, true};
    next_line(&writer);
    put(&writer, "F", milliseconds(breakdown.total_us), 3, STAT_MEASURE);
    put(&writer, "u", milliseconds(breakdown.update_us), 2, STAT_MEASURE);
    put(&writer, "d", milliseconds(breakdown.draw_us), 3, STAT_MEASURE);
    put(&writer, "s", milliseconds(breakdown.send_us), 3, STAT_MEASURE);
    if (portrait) {
        put(&writer, "h", milliseconds(parts_us[DRAW_PART_HASH]), 2, STAT_MEASURE);
        next_line(&writer);
        put(&writer, "a", milliseconds(parts_us[DRAW_PART_AUDIO]), 2, STAT_MEASURE);
    }
    for (int part = 0; part < DRAW_PART_COUNT; part++) {
        if (!portrait || (part != DRAW_PART_HASH && part != DRAW_PART_AUDIO)) {
            put(&writer, PARTS[part], milliseconds(parts_us[part]), 2, STAT_MEASURE);
        }
    }
    next_line(&writer);
    put(&writer, "b", stats.buffered_ms, 3, STAT_MEASURE);
    put(&writer, "x", stats.underruns, 3, STAT_COUNT);
    put(&writer, "o", stats.output_underruns, 3, STAT_COUNT);
    put(&writer, "L", share_percent(stats.busy_us, stats.elapsed_us), 2, STAT_PERCENT);
    put(&writer, "/", stats.core1_load, 2, STAT_PERCENT);
    if (portrait) {
        next_line(&writer);
    }
    put(&writer, "dec", average(stats.decode_total_us, stats.blocks), 2, STAT_MEASURE);
    put(&writer, "/", milliseconds(stats.decode_max_us), 3, STAT_MEASURE);
    put(&writer, "rd", average(stats.read_total_us, stats.blocks), 2, STAT_MEASURE);
    put(&writer, "/", milliseconds(stats.read_max_us), 3, STAT_MEASURE);
    return writer.line + 1;
}

void frame_overlay_draw(Canvas *canvas, const Theme *theme, FrameBreakdown breakdown,
                        HalAudioStats stats)
{
    if (rect_is_empty(rect_intersection(frame_overlay_area(theme), canvas->clip))) {
        return;
    }
    uint32_t parts[DRAW_PART_COUNT];
    for (int part = 0; part < DRAW_PART_COUNT; part++) {
        parts[part] = draw_profile_average_us((DrawPart)part);
    }
    char lines[FRAME_OVERLAY_LINES][FRAME_OVERLAY_TEXT_CAPACITY];
    int count = frame_overlay_lines(breakdown, parts, stats, theme_is_portrait(theme), lines);
    const Font *font = &FONT_MONO_12;
    int height = font_line_height(font);
    TextStyle style = {.font = font, .color = OVERLAY_TEXT};
    for (int line = 0; line < count; line++) {
        int top = line * height;
        canvas_dim_rect(canvas, (Rect){OVERLAY_LEFT - OVERLAY_PAD, top,
                                       text_width(font, lines[line]) + 2 * OVERLAY_PAD, height});
        text_draw(canvas, style, (Point){OVERLAY_LEFT, top + font->ascent}, lines[line]);
    }
}

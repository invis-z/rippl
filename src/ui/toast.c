#include "ui/toast.h"

#include <stddef.h>

#include "core/deadline.h"
#include "core/utf8.h"
#include "ui/label.h"
#include "ui/marquee.h"

enum {
    TOAST_HEIGHT = 24,
    TOAST_BOTTOM_MARGIN = 10,
    TOAST_SIDE_MARGIN = 4,
    TOAST_LINE_GAP = 2,
    BORDER = 1,
    MILLISECONDS_PER_SECOND = 1000
};

typedef struct ToastLayout {
    Rect area;
    TextSpan first;
    const char *second;
    int first_width;
    int second_width;
    int full_width;
    int line_count;
    bool scrolling;
    uint32_t duration_ms;
} ToastLayout;

static bool fit_two_lines(const Font *font, const char *text, int max_width, ToastLayout *layout)
{
    const char *best_end = NULL;
    int best_width = 0;
    int width = 0;
    const char *cursor = text;
    while (*cursor != '\0') {
        const char *before = cursor;
        uint32_t codepoint = utf8_next(&cursor);
        if (codepoint == ' ' && before > text && *(before - 1) != ' ') {
            best_end = before;
            best_width = width;
        }
        const Glyph *glyph = font_find_glyph(font, codepoint);
        if (glyph != NULL) {
            width += glyph->advance;
        }
        if (width > max_width) {
            break;
        }
    }
    if (best_end == NULL) {
        return false;
    }
    const char *second = best_end;
    while (*second == ' ') {
        second++;
    }
    if (*second == '\0') {
        return false;
    }
    int second_width = text_width(font, second);
    if (second_width > max_width) {
        return false;
    }
    layout->line_count = 2;
    layout->first = (TextSpan){text, (size_t)(best_end - text)};
    layout->second = second;
    layout->first_width = best_width;
    layout->second_width = second_width;
    return true;
}

static uint32_t scroll_duration_ms(int overflow, uint32_t speed)
{
    if (speed == 0 || overflow <= 0) {
        return TOAST_WRAPPED_DURATION_MS;
    }
    uint32_t limit = TOAST_MAX_DURATION_MS - TOAST_MARQUEE_PAUSE_MS - TOAST_MARQUEE_HOLD_MS;
    if ((uint32_t)overflow >= limit * speed / MILLISECONDS_PER_SECOND) {
        return TOAST_MAX_DURATION_MS;
    }
    uint32_t scroll_ms = ((uint32_t)overflow * MILLISECONDS_PER_SECOND + speed - 1) / speed;
    uint32_t total_ms = TOAST_MARQUEE_PAUSE_MS + scroll_ms + TOAST_MARQUEE_HOLD_MS;
    if (total_ms < TOAST_WRAPPED_DURATION_MS) {
        return TOAST_WRAPPED_DURATION_MS;
    }
    return total_ms < TOAST_MAX_DURATION_MS ? total_ms : TOAST_MAX_DURATION_MS;
}

static int text_inset(const ThemeMetrics *metrics, int width)
{
    return width > 2 * metrics->padding ? metrics->padding : BORDER;
}

static ToastLayout lay_out(const Toast *toast, const Theme *theme)
{
    const ThemeMetrics *metrics = &theme->metrics;
    const Font *font = theme->fonts.body;
    int widest = metrics->screen_width - 2 * TOAST_SIDE_MARGIN;
    int inset = text_inset(metrics, widest);
    int text_limit = widest - 2 * inset;
    int full_width = text_width(font, toast->text);
    int single_height =
        TOAST_HEIGHT < metrics->screen_height ? TOAST_HEIGHT : metrics->screen_height;
    int two_height = TOAST_HEIGHT + font_line_height(font) + TOAST_LINE_GAP;
    ToastLayout layout = {
        .full_width = full_width, .line_count = 1, .duration_ms = TOAST_DURATION_MS};
    int width = full_width + 2 * metrics->padding;
    int height = single_height;
    if (full_width <= text_limit) {
        width = width < widest ? width : widest;
    } else if (two_height <= metrics->screen_height &&
               fit_two_lines(font, toast->text, text_limit, &layout)) {
        int wider =
            layout.first_width > layout.second_width ? layout.first_width : layout.second_width;
        width = wider + 2 * metrics->padding;
        width = width < widest ? width : widest;
        height = two_height;
        layout.duration_ms = TOAST_WRAPPED_DURATION_MS;
    } else {
        width = widest;
        layout.scrolling = true;
        layout.duration_ms =
            scroll_duration_ms(full_width - text_limit, theme->marquee.speed_px_per_s);
    }
    int bottom_margin =
        metrics->screen_height > height + TOAST_BOTTOM_MARGIN ? TOAST_BOTTOM_MARGIN : 0;
    layout.area = (Rect){(metrics->screen_width - width) / 2,
                         metrics->screen_height - height - bottom_margin, width, height};
    return layout;
}

static int scroll_offset(int overflow, uint32_t speed, uint32_t elapsed_ms)
{
    if (speed == 0 || overflow <= 0) {
        return 0;
    }
    if (elapsed_ms < TOAST_MARQUEE_PAUSE_MS) {
        marquee_frame_request(TOAST_MARQUEE_PAUSE_MS - elapsed_ms);
        return 0;
    }
    uint32_t moving_ms = elapsed_ms - TOAST_MARQUEE_PAUSE_MS;
    if (moving_ms > TOAST_MAX_DURATION_MS) {
        return overflow;
    }
    uint32_t travelled = moving_ms * speed / MILLISECONDS_PER_SECOND;
    if (travelled >= (uint32_t)overflow) {
        return overflow;
    }
    uint32_t next_ms = ((travelled + 1) * MILLISECONDS_PER_SECOND + speed - 1) / speed;
    marquee_frame_request(next_ms > moving_ms ? next_ms - moving_ms : 1);
    return (int)travelled;
}

static void draw_text(Canvas *canvas, TextStyle style, Rect box, int x, const char *text,
                      size_t byte_count)
{
    int line = font_line_height(style.font);
    if (line > box.height) {
        box.y -= (line - box.height) / 2;
        box.height = line;
    }
    Canvas clipped = canvas_clipped(canvas, box);
    if (rect_is_empty(clipped.clip)) {
        return;
    }
    Point pen = {x, label_baseline(style.font, box)};
    if (byte_count > 0) {
        text_draw_span(&clipped, style, pen, (TextSpan){text, byte_count});
    } else {
        text_draw(&clipped, style, pen, text);
    }
}

void toast_show(Toast *toast, const char *text, uint32_t now_ms)
{
    *toast = (Toast){.text = text,
                     .shown_ms = now_ms,
                     .now_ms = now_ms,
                     .until_ms = now_ms + TOAST_DURATION_MS,
                     .visible = true};
}

void toast_update(Toast *toast, uint32_t now_ms)
{
    if (!toast->visible) {
        return;
    }
    toast->now_ms = now_ms;
    if (deadline_reached(now_ms, toast->until_ms)) {
        toast->visible = false;
    }
}

Rect toast_area(const Toast *toast, const Theme *theme)
{
    return lay_out(toast, theme).area;
}

void toast_draw(Toast *toast, Canvas *canvas, const Theme *theme)
{
    if (!toast->visible) {
        return;
    }
    ToastLayout layout = lay_out(toast, theme);
    if (!toast->sized) {
        toast->until_ms = toast->shown_ms + layout.duration_ms;
        toast->sized = true;
    }
    if (!deadline_reached(toast->now_ms, toast->until_ms)) {
        uint32_t remaining = toast->until_ms - toast->now_ms;
        if (remaining > 0) {
            marquee_frame_request(remaining);
        }
    }
    Rect area = layout.area;
    Rect inner = {area.x + BORDER, area.y + BORDER, area.width - 2 * BORDER,
                  area.height - 2 * BORDER};
    int inset = text_inset(&theme->metrics, area.width);
    Rect text_box = {area.x + inset, inner.y, area.width - 2 * inset, inner.height};
    int offset = 0;
    if (layout.scrolling) {
        offset = scroll_offset(layout.full_width - text_box.width, theme->marquee.speed_px_per_s,
                               toast->now_ms - toast->shown_ms);
    }
    if (rect_is_empty(rect_intersection(area, canvas->clip))) {
        return;
    }
    TextStyle style = {.font = theme->fonts.body, .color = theme->colors.header_text};
    canvas_fill_rect(canvas, area, theme->colors.divider);
    canvas_fill_rect(canvas, inner, theme->colors.header_background);
    if (layout.line_count == 2) {
        int line_height = font_line_height(style.font);
        int top = inner.y + (inner.height - (2 * line_height + TOAST_LINE_GAP)) / 2;
        Rect first_row = {text_box.x, top, text_box.width, line_height};
        Rect second_row = {text_box.x, top + line_height + TOAST_LINE_GAP, text_box.width,
                           line_height};
        draw_text(canvas, style, first_row,
                  first_row.x + (first_row.width - layout.first_width) / 2, layout.first.start,
                  layout.first.byte_count);
        draw_text(canvas, style, second_row,
                  second_row.x + (second_row.width - layout.second_width) / 2, layout.second, 0);
        return;
    }
    if (layout.scrolling && theme->marquee.speed_px_per_s > 0) {
        draw_text(canvas, style, text_box, text_box.x - offset, toast->text, 0);
        return;
    }
    Rect text_area = label_centered_area(style.font, text_box, toast->text);
    label_draw(canvas, style, (Label){.area = text_area, .text = toast->text});
}

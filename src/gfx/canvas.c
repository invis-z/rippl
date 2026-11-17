#include "gfx/canvas.h"

static Color *pixel_address(const Canvas *canvas, int x, int y)
{
    int column = x - canvas->bounds.x;
    int row = y - canvas->bounds.y;
    return &canvas->pixels[row * canvas->bounds.width + column];
}

static bool contains(Rect area, Point point)
{
    Rect single = {point.x, point.y, 1, 1};
    return !rect_is_empty(rect_intersection(area, single));
}

Canvas canvas_make(Color *pixels, Rect bounds)
{
    return (Canvas){.pixels = pixels, .bounds = bounds, .clip = bounds};
}

Canvas canvas_clipped(const Canvas *canvas, Rect area)
{
    Canvas clipped = *canvas;
    clipped.clip = rect_intersection(canvas->clip, area);
    return clipped;
}

Canvas canvas_shifted(const Canvas *canvas, int dx, Rect area)
{
    Canvas shifted = *canvas;
    shifted.bounds.x -= dx;
    shifted.clip.x -= dx;
    shifted.clip = rect_intersection(shifted.clip, area);
    return shifted;
}

void canvas_clear(Canvas *canvas, Color color)
{
    canvas_fill_rect(canvas, canvas->clip, color);
}

__attribute__((noinline)) void canvas_fill_colors(Color *dst, Color color, int count)
{
    if (count > 0 && ((uintptr_t)dst & 2u) != 0) {
        *dst++ = color;
        count--;
    }
    uint32_t pair = (uint32_t)color | (uint32_t)color << 16;
    uint32_t *words = (uint32_t *)(void *)dst;
    while (count >= 8) {
        words[0] = pair;
        words[1] = pair;
        words[2] = pair;
        words[3] = pair;
        words += 4;
        count -= 8;
    }
    while (count >= 2) {
        *words++ = pair;
        count -= 2;
    }
    if (count > 0) {
        *(Color *)(void *)words = color;
    }
}

void canvas_fill_rect(Canvas *canvas, Rect area, Color color)
{
    Rect visible = rect_intersection(canvas->clip, area);
    if (rect_is_empty(visible)) {
        return;
    }
    int stride = canvas->bounds.width;
    Color *row = pixel_address(canvas, visible.x, visible.y);
    for (int y = 0; y < visible.height; y++, row += stride) {
        canvas_fill_colors(row, color, visible.width);
    }
}

void canvas_fill_gradient(Canvas *canvas, Rect area, Color top, Color bottom)
{
    for (int row = 0; row < area.height; row++) {
        Color color = color_between(top, bottom, row, area.height - 1);
        canvas_fill_rect(canvas, (Rect){area.x, area.y + row, area.width, 1}, color);
    }
}

void canvas_blend_pixel(Canvas *canvas, Point point, Color color, uint8_t alpha)
{
    if (!contains(canvas->clip, point)) {
        return;
    }
    Color *pixel = pixel_address(canvas, point.x, point.y);
    *pixel = color_blend(*pixel, color, alpha);
}

Color canvas_pixel_at(const Canvas *canvas, int x, int y)
{
    return contains(canvas->bounds, (Point){x, y}) ? *pixel_address(canvas, x, y) : 0;
}

void canvas_outline_rect(Canvas *canvas, Rect area, int thickness, Color color)
{
    canvas_fill_rect(canvas, (Rect){area.x, area.y, area.width, thickness}, color);
    canvas_fill_rect(
        canvas, (Rect){area.x, area.y + area.height - thickness, area.width, thickness}, color);
    canvas_fill_rect(canvas, (Rect){area.x, area.y, thickness, area.height}, color);
    canvas_fill_rect(
        canvas, (Rect){area.x + area.width - thickness, area.y, thickness, area.height}, color);
}

void canvas_dim_rect(Canvas *canvas, Rect area)
{
    Rect visible = rect_intersection(canvas->clip, area);
    if (rect_is_empty(visible)) {
        return;
    }
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        Color *row = pixel_address(canvas, visible.x, y);
        for (int column = 0; column < visible.width; column++) {
            row[column] = (Color)((row[column] >> 1) & 0x7BEFu);
        }
    }
}

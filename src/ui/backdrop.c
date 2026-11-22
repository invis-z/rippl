#include "ui/backdrop.h"

#include <stddef.h>

#include "core/draw_profile.h"
#include "core/resident.h"
#include "ui/theme_engine.h"

typedef struct Run {
    int column;
    int count;
} Run;

RIPPL_ENGINE_CODE static Color single(const Wallpaper *wallpaper, const uint8_t *row, int column)
{
    uint32_t both = wallpaper->pairs[row[column / 2]];
    return (Color)(column % 2 == 0 ? both : both >> 16);
}

RIPPL_ENGINE_CODE static Color *paint_tile(Color *target, const Wallpaper *wallpaper,
                                           const uint8_t *row, Run run)
{
    int column = run.column;
    int end = run.column + run.count;
    if (column % 2 != 0 && column < end) {
        *target++ = single(wallpaper, row, column++);
    }
    const uint8_t *pair = row + column / 2;
    for (; column + 1 < end; column += 2) {
        __builtin_memcpy(target, &wallpaper->pairs[*pair++], sizeof(uint32_t));
        target += 2;
    }
    if (column < end) {
        *target++ = single(wallpaper, row, column);
    }
    return target;
}

RIPPL_ENGINE_CODE static void paint_row(Color *target, const Wallpaper *wallpaper,
                                        const uint8_t *row, Run run)
{
    int width = wallpaper->width;
    int column = (run.column % width + width) % width;
    for (int left = run.count; left > 0; column = 0) {
        int count = left < width - column ? left : width - column;
        target = paint_tile(target, wallpaper, row, (Run){column, count});
        left -= count;
    }
}

RIPPL_ENGINE_CODE static void paint_wallpaper(Canvas *canvas, const Wallpaper *wallpaper, Rect area)
{
    Rect visible = rect_intersection(area, canvas->clip);
    int stride = wallpaper->width / 2;
    for (int y = visible.y; y < visible.y + visible.height; y++) {
        int across = visible.x - canvas->bounds.x;
        Color *target = &canvas->pixels[(y - canvas->bounds.y) * canvas->bounds.width + across];
        const uint8_t *row = wallpaper->rows + (y % wallpaper->height) * stride;
        paint_row(target, wallpaper, row, (Run){visible.x, visible.width});
    }
}

RIPPL_ENGINE_CODE void theme_engine_wallpaper(Canvas *canvas, const Wallpaper *wallpaper, Rect area)
{
    paint_wallpaper(canvas, wallpaper, area);
}

void backdrop_paint(Canvas *canvas, const Theme *theme, Rect area)
{
    uint32_t started_us = draw_profile_now();
    const Wallpaper *wallpaper = theme->wallpaper;
    const ThemeEngine *engine = theme_engine();
    if (wallpaper == NULL || !wallpaper->ready || engine == NULL) {
        canvas_fill_rect(canvas, area, theme->colors->background);
    } else if (!canvas->backdrop_painted) {
        engine->wallpaper(canvas, wallpaper, area);
    }
    draw_profile_since(DRAW_PART_BACKDROP, started_us);
}

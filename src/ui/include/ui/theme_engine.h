#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/icons.h"
#include "ui/theme.h"

enum {
    THEME_ENGINE_MAGIC = 0x474E4554
};

typedef struct ThemeEngine {
    uint32_t magic;
    void (*box)(Canvas *canvas, const ThemeColors *colors, const BoxStyle *style, Rect area);
    void (*wallpaper)(Canvas *canvas, const Wallpaper *wallpaper, Rect area);
    void (*mask)(Canvas *canvas, const IconMask *mask, Icon icon);
} ThemeEngine;

extern const ThemeEngine THEME_ENGINE;

void theme_engine_use(const ThemeEngine *engine);
const ThemeEngine *theme_engine(void);
void theme_engine_box(Canvas *canvas, const ThemeColors *colors, const BoxStyle *style, Rect area);
void theme_engine_wallpaper(Canvas *canvas, const Wallpaper *wallpaper, Rect area);
void theme_engine_mask(Canvas *canvas, const IconMask *mask, Icon icon);

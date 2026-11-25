#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "gfx/canvas.h"
#include "ui/theme.h"

enum {
    TOAST_DURATION_MS = 2000,
    TOAST_WRAPPED_DURATION_MS = 3000,
    TOAST_MARQUEE_PAUSE_MS = 500,
    TOAST_MARQUEE_HOLD_MS = 800,
    TOAST_MAX_DURATION_MS = 7000
};

typedef struct Toast {
    const char *text;
    uint32_t shown_ms;
    uint32_t now_ms;
    uint32_t until_ms;
    bool visible;
    bool sized;
} Toast;

void toast_show(Toast *toast, const char *text, uint32_t now_ms);
void toast_update(Toast *toast, uint32_t now_ms);
Rect toast_area(const Toast *toast, const Theme *theme);
void toast_draw(Toast *toast, Canvas *canvas, const Theme *theme);
void toast_draw_plain(const Toast *toast, Canvas *canvas, const Theme *theme);

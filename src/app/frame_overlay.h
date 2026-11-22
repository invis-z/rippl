#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/draw_profile.h"
#include "gfx/canvas.h"
#include "hal/audio.h"
#include "ui/theme.h"

enum {
    FRAME_OVERLAY_LINES = 5,
    FRAME_OVERLAY_TEXT_CAPACITY = 48
};

typedef struct FrameBreakdown {
    uint32_t total_us;
    uint32_t update_us;
    uint32_t draw_us;
    uint32_t send_us;
} FrameBreakdown;

Rect frame_overlay_area(const Theme *theme);
Color frame_overlay_background(void);
Color frame_overlay_text_color(void);
int frame_overlay_lines(FrameBreakdown breakdown, const uint32_t *parts_us, HalAudioStats stats,
                        bool portrait,
                        char lines[FRAME_OVERLAY_LINES][FRAME_OVERLAY_TEXT_CAPACITY]);
void frame_overlay_draw(Canvas *canvas, const Theme *theme, FrameBreakdown breakdown,
                        HalAudioStats stats);

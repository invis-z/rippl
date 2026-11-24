#include "ui/rippl_logo.h"

#include <stddef.h>

#include "rippl_logo_data.h"

_Static_assert((int)RIPPL_LOGO_LEVELS == (int)COLOR_ALPHA_OPAQUE, "a logo level is a blend alpha");

enum {
    LOGO_LABEL_MARGIN = 3,
    LOGO_LABEL_BASE = 48,
    LOGO_LABEL_SIDE = LOGO_LABEL_BASE + 2 * LOGO_LABEL_MARGIN,
    LOGO_LABEL_MAGIC_A = 0x4C424C52u,
    LOGO_LABEL_MAGIC_B = 0x524C424Cu
};

typedef struct LogoCoverCache {
    uint32_t magic_a;
    uint32_t magic_b;
    Color background;
    Color foreground;
    Color pixels[LOGO_LABEL_SIDE * LOGO_LABEL_SIDE];
} LogoCoverCache;

const RipplLogo RIPPL_LOGO_SPLASH = {RIPPL_LOGO_SPLASH_COVERAGE, RIPPL_LOGO_SPLASH_SIDE};
const RipplLogo RIPPL_LOGO_LABEL = {RIPPL_LOGO_LABEL_COVERAGE, RIPPL_LOGO_LABEL_SIDE};

uint8_t rippl_logo_alpha(const RipplLogo *logo, int x, int y)
{
    if (x < 0 || y < 0 || x >= logo->side || y >= logo->side) {
        return 0;
    }
    int index = y * logo->side + x;
    uint8_t pair = logo->coverage[index / 2];
    return (uint8_t)((index % 2 == 0 ? pair : pair >> 4) & RIPPL_LOGO_LEVELS);
}

void rippl_logo_draw(Canvas *canvas, const RipplLogo *logo, Point origin, Color color,
                     uint8_t alpha)
{
    for (int y = 0; y < logo->side; y++) {
        for (int x = 0; x < logo->side; x++) {
            uint8_t coverage = rippl_logo_alpha(logo, x, y);
            if (coverage != 0) {
                canvas_blend_pixel(canvas, (Point){origin.x + x, origin.y + y}, color,
                                   (uint8_t)(coverage * alpha / COLOR_ALPHA_OPAQUE));
            }
        }
    }
}

__attribute__((noinline)) Image rippl_logo_cover(void *scratch, uint32_t scratch_size,
                                                 Color background, Color foreground)
{
    if (scratch == NULL || scratch_size < sizeof(LogoCoverCache)) {
        return (Image){0};
    }
    uintptr_t end = (uintptr_t)scratch + scratch_size;
    uintptr_t at = (end - sizeof(LogoCoverCache)) & ~(uintptr_t)3u;
    if (at < (uintptr_t)scratch) {
        return (Image){0};
    }
    LogoCoverCache *cache = (LogoCoverCache *)at;
    if (cache->magic_a != LOGO_LABEL_MAGIC_A || cache->magic_b != LOGO_LABEL_MAGIC_B ||
        cache->background != background || cache->foreground != foreground) {
        Color palette[COLOR_ALPHA_OPAQUE + 1];
        for (uint8_t level = 0; level <= COLOR_ALPHA_OPAQUE; level++) {
            palette[level] = color_blend(background, foreground, level);
        }
        canvas_fill_colors(cache->pixels, background, LOGO_LABEL_SIDE * LOGO_LABEL_SIDE);
        int side =
            RIPPL_LOGO_LABEL.side < LOGO_LABEL_BASE ? RIPPL_LOGO_LABEL.side : LOGO_LABEL_BASE;
        for (int y = 0; y < side; y++) {
            Color *dst =
                &cache->pixels[(y + LOGO_LABEL_MARGIN) * LOGO_LABEL_SIDE + LOGO_LABEL_MARGIN];
            for (int x = 0; x < side; x++) {
                dst[x] = palette[rippl_logo_alpha(&RIPPL_LOGO_LABEL, x, y)];
            }
        }
        cache->background = background;
        cache->foreground = foreground;
        cache->magic_a = LOGO_LABEL_MAGIC_A;
        cache->magic_b = LOGO_LABEL_MAGIC_B;
    }
    return (Image){cache->pixels, LOGO_LABEL_SIDE, LOGO_LABEL_SIDE};
}

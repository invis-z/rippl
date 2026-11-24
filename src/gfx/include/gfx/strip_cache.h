#pragma once

#include <stdint.h>

#include "gfx/canvas.h"
#include "gfx/rect.h"

typedef enum StripCacheAction {
    STRIP_CACHE_NONE,
    STRIP_CACHE_START,
    STRIP_CACHE_CONTINUE,
    STRIP_CACHE_REPLAY
} StripCacheAction;

typedef struct StripCacheKey {
    const void *owner;
    uint32_t magic;
    uint32_t stamp;
    uint32_t owner_size;
    uint32_t meta;
    Rect area;
} StripCacheKey;

StripCacheAction strip_cache_open(void *memory, uint32_t size, const Canvas *canvas,
                                  const StripCacheKey *key, uint16_t *tag);
void strip_cache_capture(void *memory, uint32_t size, const Canvas *canvas, uint16_t tag);
void strip_cache_blit(const void *memory, Canvas *canvas);
void strip_cache_abort(void *memory);

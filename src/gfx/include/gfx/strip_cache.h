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

typedef enum StripListAction {
    STRIP_LIST_NONE,
    STRIP_LIST_RECORD,
    STRIP_LIST_REPLAY
} StripListAction;

typedef struct StripListKey {
    StripCacheKey cache;
    const void *cover;
    const void *theme;
    const void *menu;
    uint32_t now_ms;
    uint32_t shown_ms;
    uint32_t position_ms;
    uint32_t duration_ms;
    uint32_t tick;
    uint32_t flags;
    uint32_t extra;
    uint32_t magic;
} StripListKey;

typedef struct StripList StripList;

StripCacheAction strip_cache_open(void *memory, uint32_t size, const Canvas *canvas,
                                  const StripCacheKey *key, uint16_t *tag);
void strip_cache_capture(void *memory, uint32_t size, const Canvas *canvas, uint16_t tag);
void strip_cache_blit(const void *memory, Canvas *canvas);
void strip_cache_abort(void *memory);
StripListAction strip_list_open(void *memory, uint32_t size, Rect shown, const StripListKey *key,
                                uint32_t payload_bytes, StripList **out, void **payload,
                                const int32_t **cmds, uint32_t *words);
void strip_list_push(StripList *list, uint32_t header, const int32_t *values, uint32_t count);

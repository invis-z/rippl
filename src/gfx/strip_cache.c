#include "gfx/strip_cache.h"

#include <stdbool.h>
#include <stddef.h>

enum {
    CACHE_MAGIC = 0x43484353,
    CACHE_LITERAL_BIT = 0x8000,
    CACHE_MAX_RUN = 0x7FFF,
    CACHE_MAX_WORDS = 0xFFFF
};

typedef enum CacheState {
    CACHE_BUILDING,
    CACHE_READY,
    CACHE_NONE,
    CACHE_OVERFLOW
} CacheState;

typedef struct StripCache {
    StripCacheKey key;
    uint16_t tag;
    uint16_t built_rows;
    uint16_t data_words;
    uint16_t last_words;
    uint8_t state;
    uint8_t pad[3];
} StripCache;

_Static_assert(sizeof(StripCacheKey) == sizeof(const void *) + 32,
               "the strip cache key has no padding bytes");

static Color *pixel_at(const Canvas *canvas, int x, int y)
{
    return &canvas->pixels[(y - canvas->bounds.y) * canvas->bounds.width + x - canvas->bounds.x];
}

static void fill_colors(Color *dst, Color color, int count)
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

__attribute__((noinline)) StripCacheAction strip_cache_open(void *memory, uint32_t size,
                                                            const Canvas *canvas,
                                                            const StripCacheKey *requested,
                                                            uint16_t *tag)
{
    Rect area = requested->area;
    Rect shown = rect_intersection(canvas->clip, area);
    if (rect_is_empty(shown) || area.width <= 0 || area.width > CACHE_MAX_RUN || area.height <= 0) {
        return STRIP_CACHE_NONE;
    }
    uint32_t base =
        (uint32_t)sizeof(StripCache) + (uint32_t)area.height * (uint32_t)sizeof(uint16_t);
    if (memory == NULL || ((uintptr_t)memory & 3u) != 0 || size <= base) {
        return STRIP_CACHE_NONE;
    }
    StripCache *cache = memory;
    StripCacheKey key = *requested;
    key.magic = CACHE_MAGIC;
    bool full_row = shown.x == area.x && shown.width == area.width;
    if (__builtin_memcmp(&cache->key, &key, sizeof key) == 0) {
        if (cache->state == CACHE_READY) {
            *tag = cache->tag;
            return STRIP_CACHE_REPLAY;
        }
        if (cache->state == CACHE_NONE || cache->state == CACHE_OVERFLOW) {
            return STRIP_CACHE_NONE;
        }
        if (full_row && shown.y == area.y + cache->built_rows && cache->tag > 0) {
            *tag = cache->tag;
            return STRIP_CACHE_CONTINUE;
        }
    }
    if (!full_row || shown.y != area.y) {
        return STRIP_CACHE_NONE;
    }
    *cache = (StripCache){.key = key, .state = CACHE_BUILDING};
    return STRIP_CACHE_START;
}

__attribute__((noinline)) void strip_cache_capture(void *memory, uint32_t size,
                                                   const Canvas *canvas, uint16_t tag)
{
    StripCache *cache = memory;
    if (tag == 0) {
        cache->state = CACHE_NONE;
        return;
    }
    cache->tag = tag;
    Rect area = cache->key.area;
    Rect shown = rect_intersection(canvas->clip, area);
    uint32_t base =
        (uint32_t)sizeof(StripCache) + (uint32_t)area.height * (uint32_t)sizeof(uint16_t);
    uint32_t capacity = (size - base) / (uint32_t)sizeof(uint16_t);
    if (capacity > CACHE_MAX_WORDS) {
        capacity = CACHE_MAX_WORDS;
    }
    uint16_t *rows = (uint16_t *)(void *)(cache + 1);
    uint16_t *data = rows + area.height;
    const Color *pixels = pixel_at(canvas, shown.x, shown.y);
    int stride = canvas->bounds.width;
    for (int row = 0; row < shown.height; row++, pixels += stride) {
        int index = shown.y - area.y + row;
        uint32_t start = cache->data_words;
        uint32_t out = start;
        for (int x = 0; x < area.width;) {
            int begin = x;
            Color color = pixels[x];
            if (x + 1 < area.width && pixels[x + 1] == color) {
                while (x < area.width && pixels[x] == color) {
                    x++;
                }
                if (out + 2u > capacity) {
                    cache->state = CACHE_OVERFLOW;
                    return;
                }
                data[out++] = (uint16_t)(x - begin);
                data[out++] = color;
            } else {
                x++;
                while (x < area.width && !(x + 2 < area.width && pixels[x] == pixels[x + 1] &&
                                           pixels[x] == pixels[x + 2])) {
                    x++;
                }
                uint32_t count = (uint32_t)(x - begin);
                if (out + 1u + count > capacity) {
                    cache->state = CACHE_OVERFLOW;
                    return;
                }
                data[out++] = (uint16_t)(CACHE_LITERAL_BIT | count);
                __builtin_memcpy(data + out, pixels + begin, count * sizeof(uint16_t));
                out += count;
            }
        }
        uint32_t written = out - start;
        if (index > 0 && written == cache->last_words &&
            __builtin_memcmp(data + rows[index - 1], data + start, written * sizeof(uint16_t)) ==
                0) {
            rows[index] = rows[index - 1];
        } else {
            rows[index] = (uint16_t)start;
            cache->data_words = (uint16_t)out;
            cache->last_words = (uint16_t)written;
        }
        cache->built_rows = (uint16_t)(index + 1);
    }
    if (cache->built_rows == (uint16_t)area.height) {
        cache->state = CACHE_READY;
    }
}

__attribute__((noinline)) void strip_cache_blit(const void *memory, Canvas *canvas)
{
    const StripCache *cache = memory;
    Rect area = cache->key.area;
    Rect shown = rect_intersection(canvas->clip, area);
    const uint16_t *rows = (const uint16_t *)(const void *)(cache + 1);
    const uint16_t *data = rows + area.height;
    const uint16_t *end = data + cache->data_words;
    Color *row_dst = pixel_at(canvas, shown.x, shown.y);
    int stride = canvas->bounds.width;
    for (int row = 0; row < shown.height; row++, row_dst += stride) {
        Color *dst = row_dst;
        const uint16_t *src = data + rows[shown.y - area.y + row];
        int skip = shown.x - area.x;
        int need = shown.width;
        while (need > 0 && src < end) {
            uint16_t tag = *src++;
            bool literal = (tag & CACHE_LITERAL_BIT) != 0;
            int len = tag & CACHE_MAX_RUN;
            if (literal ? end - src < len : src >= end) {
                break;
            }
            const uint16_t *lit = src;
            Color color = literal ? 0 : *src;
            src += literal ? len : 1;
            if (skip >= len) {
                skip -= len;
                continue;
            }
            lit += skip;
            len -= skip;
            skip = 0;
            int take = len < need ? len : need;
            if (literal) {
                __builtin_memcpy(dst, lit, (size_t)take * sizeof(Color));
            } else {
                fill_colors(dst, color, take);
            }
            dst += take;
            need -= take;
        }
    }
}

__attribute__((noinline)) void strip_cache_abort(void *memory)
{
    StripCache *cache = memory;
    cache->state = CACHE_NONE;
}

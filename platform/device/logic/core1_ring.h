#pragma once

#include <stdint.h>

#include "codec/flac.h"

enum {
    WORD_CHANNELS = 2
};

static inline void publish(void)
{
    __atomic_thread_fence(__ATOMIC_RELEASE);
}

static inline void observe(void)
{
    __atomic_thread_fence(__ATOMIC_ACQUIRE);
}

static inline uint32_t smaller(uint32_t a, uint32_t b)
{
    return a < b ? a : b;
}

static inline int32_t *slot_row(int32_t *samples, uint32_t offset, uint32_t stride, uint32_t slot,
                                uint32_t channel)
{
    return samples + offset + (slot * FLAC_MAX_CHANNELS + channel) * stride;
}

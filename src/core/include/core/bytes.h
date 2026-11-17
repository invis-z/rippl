#pragma once

#include <stdint.h>

static inline uint32_t bytes_get16(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] | (uint32_t)bytes[1] << 8;
}

static inline uint32_t bytes_get32(const uint8_t *bytes)
{
    return bytes_get16(bytes) | bytes_get16(bytes + 2) << 16;
}

static inline uint32_t bytes_get16_be(const uint8_t *bytes)
{
    return (uint32_t)bytes[0] << 8 | bytes[1];
}

static inline uint32_t bytes_get32_be(const uint8_t *bytes)
{
    return bytes_get16_be(bytes) << 16 | bytes_get16_be(bytes + 2);
}

static inline void bytes_put16(uint8_t *bytes, uint32_t value)
{
    bytes[0] = (uint8_t)value;
    bytes[1] = (uint8_t)(value >> 8);
}

static inline void bytes_put32(uint8_t *bytes, uint32_t value)
{
    bytes_put16(bytes, value & 0xFFFFu);
    bytes_put16(bytes + 2, value >> 16);
}

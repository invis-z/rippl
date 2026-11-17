#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "core/bytes.h"
#include "core/hash.h"

static inline bool record_magic_ok(const uint8_t *bytes, const uint8_t *magic, uint32_t size)
{
    for (uint32_t index = 0; index < size; index++) {
        if (bytes[index] != magic[index]) {
            return false;
        }
    }
    return true;
}

static inline void record_put_magic(uint8_t *bytes, const uint8_t *magic, uint32_t size)
{
    for (uint32_t index = 0; index < size; index++) {
        bytes[index] = magic[index];
    }
}

static inline bool record_checksum_ok(const uint8_t *bytes, uint32_t field)
{
    return bytes_get32(bytes + field) == hash_update(HASH_START, bytes, field);
}

static inline void record_seal(uint8_t *bytes, uint32_t field)
{
    bytes_put32(bytes + field, hash_update(HASH_START, bytes, field));
}

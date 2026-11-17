#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "codec/flac.h"
#include "codec/mp4.h"

static inline bool core1_mp4_record(FlacInput input, uint32_t position, uint32_t *start,
                                    uint32_t *length)
{
    uint8_t bytes[MP4_LENGTH_BYTES];
    if (!input.read(input.context, position, bytes, sizeof bytes)) {
        return false;
    }
    *start = position + MP4_LENGTH_BYTES;
    *length = mp4_record_length(bytes);
    if (*length == 0 || *start > input.size || *length > input.size - *start) {
        return false;
    }
    uint8_t last = 0;
    return input.read(input.context, *start + *length - 1, &last, 1);
}

#pragma once

#include <stddef.h>
#include <stdint.h>

enum {
    UTF8_REPLACEMENT_CHARACTER = 0xFFFD
};

uint32_t utf8_next(const char **cursor);
size_t utf8_complete_length(const char *text, size_t length);

static inline uint32_t utf8_encoded_size(uint32_t code_point)
{
    if (code_point < 0x80) {
        return 1;
    }
    if (code_point < 0x800) {
        return 2;
    }
    return code_point < 0x10000 ? 3 : 4;
}

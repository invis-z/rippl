#pragma once

#include <stdbool.h>
#include <stdint.h>

static inline char text_lower(char character)
{
    return character >= 'A' && character <= 'Z' ? (char)(character - 'A' + 'a') : character;
}

static inline bool text_is_space(uint32_t code_point)
{
    return code_point == ' ' || (code_point >= 0x09 && code_point <= 0x0D) ||
           (code_point >= 0x1C && code_point <= 0x1F) || code_point == 0x85 || code_point == 0xA0 ||
           code_point == 0x1680 || (code_point >= 0x2000 && code_point <= 0x200A) ||
           code_point == 0x2028 || code_point == 0x2029 || code_point == 0x202F ||
           code_point == 0x205F || code_point == 0x3000;
}

static inline uint32_t text_length(const char *text)
{
    uint32_t length = 0;
    while (text[length] != '\0') {
        length++;
    }
    return length;
}

static inline uint32_t text_component_length(const char *path)
{
    uint32_t length = 0;
    while (path[length] != '\0' && path[length] != '/') {
        length++;
    }
    return length;
}

static inline bool text_same(const char *first, const char *second)
{
    uint32_t index = 0;
    while (first[index] != '\0' && first[index] == second[index]) {
        index++;
    }
    return first[index] == second[index];
}

static inline int text_compare(const char *left, const char *right)
{
    const unsigned char *a = (const unsigned char *)left;
    const unsigned char *b = (const unsigned char *)right;
    while (*a != '\0' && *a == *b) {
        a++;
        b++;
    }
    return (int)*a - (int)*b;
}

static inline uint32_t text_progress_width(uint32_t position_ms, uint32_t duration_ms, int width)
{
    if (duration_ms == 0) {
        return 0;
    }
    if (position_ms >= duration_ms) {
        return (uint32_t)width;
    }
    return (uint32_t)((uint64_t)position_ms * (uint64_t)width / duration_ms);
}

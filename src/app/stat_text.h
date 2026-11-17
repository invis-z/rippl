#pragma once

#include <stdint.h>

#include "core/string_builder.h"

typedef enum StatKind {
    STAT_MEASURE,
    STAT_COUNT,
    STAT_PERCENT
} StatKind;

static inline void stat_text_put(StringBuilder *builder, const char *label, uint32_t value,
                                 uint32_t digits, StatKind kind)
{
    uint32_t limit = 1;
    for (uint32_t digit = 0; digit < digits; digit++) {
        limit *= 10u;
    }
    if (kind == STAT_COUNT && value >= limit && label[0] >= 'a' && label[0] <= 'z') {
        string_builder_append_char(builder, (char)(label[0] - 'a' + 'A'));
        string_builder_append(builder, label + 1);
    } else {
        string_builder_append(builder, label);
    }
    if (kind == STAT_PERCENT && value >= limit) {
        string_builder_append(builder, "xx%");
        return;
    }
    value = kind == STAT_COUNT ? value % limit : value < limit ? value : limit - 1u;
    for (uint32_t power = limit / 10u; power > 0; power /= 10u) {
        string_builder_append_char(builder, (char)('0' + value / power % 10u));
    }
    if (kind == STAT_PERCENT) {
        string_builder_append_char(builder, '%');
    }
}

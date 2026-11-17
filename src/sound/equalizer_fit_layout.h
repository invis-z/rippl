#pragma once

#include <stdint.h>

#include "core/bytes.h"

enum {
    FIT_VERSION = 1,
    FIT_METHOD = 2,
    FIT_MAGIC_SIZE = 4,
    VERSION_FIELD = 4,
    ENTRIES_FIELD = 5,
    KEY_FIELD = 8,
    TARGET_FIELD = 12,
    RATE_FIELD = 0,
    WORST_FIELD = 4,
    COUNT_FIELD = 6,
    STAGES_FIELD = 8,
    STAGE_G = 0,
    STAGE_A1 = 4,
    STAGE_M1 = 8,
    STAGE_M2 = 12,
    STAGE_BAND = 16,
    FINE_FLAG = 0x80
};

static const uint8_t FIT_MAGIC[FIT_MAGIC_SIZE] = {'E', 'Q', 'F', 'T'};

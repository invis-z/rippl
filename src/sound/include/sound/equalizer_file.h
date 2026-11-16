#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "sound/equalizer_presets.h"

enum {
    EQUALIZER_FILE_SIZE = 75,
    EQUALIZER_PRESET_NAME_CAPACITY = 64,
    EQUALIZER_MAP_SLOTS = 100,
    EQUALIZER_MAP_OFFSET = EQUALIZER_FILE_SIZE,
    EQUALIZER_MAP_SIZE = EQUALIZER_MAP_SLOTS * 4 + 4,
    EQUALIZER_FILE_WITH_MAP = EQUALIZER_MAP_OFFSET + EQUALIZER_MAP_SIZE,
    EQUALIZER_MAP_ABSENT = -1,
    EQUALIZER_MAP_UNKNOWN = -2
};

typedef struct EqualizerChoice {
    EqualizerPreset preset;
    char preset_name[EQUALIZER_PRESET_NAME_CAPACITY];
} EqualizerChoice;

EqualizerChoice equalizer_file_defaults(void);
void equalizer_file_encode(const EqualizerChoice *choice, uint8_t *bytes);
bool equalizer_file_decode(const uint8_t *bytes, uint32_t length, EqualizerChoice *choice);
void equalizer_map_clear(uint8_t *map);
void equalizer_map_put(uint8_t *map, int slot, uint32_t key);
void equalizer_map_seal(uint8_t *map);
int equalizer_map_find(const uint8_t *map, uint32_t key);

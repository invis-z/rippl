#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "equalizer_store.h"
#include "hal/storage.h"

enum {
    EQUALIZER_FILES_MAX = 100,
    EQUALIZER_FILES_NAME_CAPACITY = EQUALIZER_PRESET_NAME_CAPACITY + 4
};

typedef struct EqualizerFileList {
    int count;
} EqualizerFileList;

void equalizer_files_scan(EqualizerFileList *list);
bool equalizer_files_open(HalDir *dir);
bool equalizer_files_next(HalDir *dir, char *name);
bool equalizer_files_name(const EqualizerFileList *list, int index, char *name);
bool equalizer_files_stem(const EqualizerFileList *list, int index, char *stem);
int equalizer_files_find(const EqualizerFileList *list, const char *name);
uint32_t equalizer_files_stem_length(const char *name);

#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "dsp/equalizer.h"
#include "hal/storage.h"
#include "sound/equalizer_fit_file.h"

enum {
    EQUALIZER_FITS_SLOTS = 100,
    EQUALIZER_FITS_SLOT_BYTES = 4096,
    EQUALIZER_FITS_PATH_CAPACITY = 32
};

#define EQUALIZER_FITS_MAGIC 0x46514545u

extern const char EQUALIZER_FITS_FOLDER[];

typedef struct EqualizerFitsProgress {
    void (*show)(void *context, uint32_t preset, uint32_t presets, uint32_t step, uint32_t steps);
    void *context;
} EqualizerFitsProgress;

typedef struct EqualizerFitsReport {
    uint32_t presets;
    uint32_t fitted;
    uint32_t failed;
} EqualizerFitsReport;

typedef struct EqualizerFitsRequest {
    const char *only;
    bool all;
    bool log;
} EqualizerFitsRequest;

typedef struct EqualizerFitsEntry {
    uint32_t magic;
    EqualizerFitsReport (*refresh)(EqualizerFitsProgress progress, EqualizerFitsRequest request);
} EqualizerFitsEntry;

extern const EqualizerFitsEntry EQUALIZER_FITS_ENTRY;

EqualizerFitsReport equalizer_fits_refresh(EqualizerFitsProgress progress,
                                           EqualizerFitsRequest request);
uint32_t equalizer_fits_apply(const EqualizerCurve *curve);
bool equalizer_fits_read(uint32_t slot, uint32_t key, uint32_t rate, EqualizerFit *fit);
void equalizer_fits_slot_path(char *path, int slot);
bool equalizer_fits_open_slot(HalFile *file, int slot, EqualizerFitHeader *header);

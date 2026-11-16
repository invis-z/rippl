#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dsp/equalizer_fit.h"
#include "equalizer_band.h"
#include "sound/equalizer_fit_file.h"
#include "sound/equalizer_text.h"

enum {
    TEXT_CAPACITY = EQUALIZER_TEXT_MAX_BYTES + 1,
    FILE_CAPACITY = 8192,
    NO_FIT = 0xFFFF
};

static uint8_t text[TEXT_CAPACITY];
static uint8_t fit_bytes[FILE_CAPACITY];
static int64_t work[8192];

static size_t read_all(const char *path, uint8_t *buffer, size_t capacity)
{
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return 0;
    }
    size_t length = fread(buffer, 1, capacity, file);
    fclose(file);
    return length;
}

static bool load_curve(const char *path, EqualizerCurve *curve)
{
    size_t length = read_all(path, text, sizeof text);
    EqualizerTextError error;
    return length > 0 && equalizer_text_parse((const char *)text, (uint32_t)length, curve, &error);
}

static void print_curve(const EqualizerCurve *curve)
{
    printf("key %u target %u bands %u\n", equalizer_curve_key(curve), equalizer_target_rate(curve),
           curve->band_count);
    for (int index = 0; index < curve->band_count; index++) {
        EqualizerBand band = curve->bands[index];
        printf("band %d %u %d %u %u\n", index, band.frequency_hz, band.gain_tenths,
               band.q_hundredths, band.filter);
    }
}

static void print_designs(const EqualizerCurve *curve)
{
    for (uint32_t rate = 0; rate < EQUALIZER_FIT_RATES; rate++) {
        uint32_t hertz = EQUALIZER_FIT_RATE_LIST[rate];
        for (int index = 0; index < curve->band_count; index++) {
            if (!equalizer_band_audible(curve->bands[index], hertz)) {
                continue;
            }
            EqualizerStage stage = equalizer_band_design(curve->bands[index], hertz);
            printf("design %u %d %d %d %d %d %u\n", hertz, index, stage.g, stage.a1, stage.m1,
                   stage.m2, stage.fine);
        }
    }
}

static void print_fits(const EqualizerCurve *curve)
{
    EqualizerFit seed = {0};
    bool seeded = false;
    for (uint32_t rate = EQUALIZER_FIT_RATES; rate-- > 0;) {
        uint32_t hertz = EQUALIZER_FIT_RATE_LIST[rate];
        if (!equalizer_fit_needed(curve, hertz)) {
            continue;
        }
        EqualizerFit fit;
        EqualizerFitStats stats;
        bool found = equalizer_fit(curve, hertz, work, sizeof work, &fit, &stats, NULL,
                                   seeded ? &seed : NULL);
        printf("fit %u %u %u\n", hertz, stats.today_hundredths,
               found ? fit.worst_hundredths : NO_FIT);
        if (found) {
            seed = fit;
            seeded = true;
        }
    }
}

static int check_file(const EqualizerCurve *curve, const char *path)
{
    size_t length = read_all(path, fit_bytes, sizeof fit_bytes);
    EqualizerFitHeader header;
    if (length < EQUALIZER_FIT_FILE_HEADER || !equalizer_fit_file_read_header(fit_bytes, &header)) {
        printf("bad header\n");
        return 1;
    }
    if (length < equalizer_fit_file_size(header.entries)) {
        printf("bad size\n");
        return 1;
    }
    uint32_t checked = equalizer_fit_file_size(header.entries) - EQUALIZER_FIT_FILE_CHECKSUM;
    uint32_t checksum =
        equalizer_fit_file_checksum(equalizer_fit_file_checksum_start(), fit_bytes, checked);
    if (equalizer_fit_file_stored_checksum(fit_bytes + checked) != checksum) {
        printf("bad checksum\n");
        return 1;
    }
    if (header.key != equalizer_curve_key(curve) ||
        header.target_rate != equalizer_target_rate(curve)) {
        printf("bad key\n");
        return 1;
    }
    for (uint32_t index = 0; index < header.entries; index++) {
        EqualizerFit fit;
        const uint8_t *entry =
            fit_bytes + EQUALIZER_FIT_FILE_HEADER + index * EQUALIZER_FIT_FILE_ENTRY;
        if (!equalizer_fit_file_read_entry(entry, header.target_rate, &fit)) {
            printf("bad entry %u\n", index);
            return 1;
        }
        printf("entry %u %u %u\n", fit.rate, fit.worst_hundredths, fit.stage_count);
    }
    printf("ok %u\n", header.entries);
    return 0;
}

int main(int argc, char **argv)
{
    EqualizerCurve curve;
    if (argc < 3 || !load_curve(argv[2], &curve)) {
        fprintf(stderr, "usage: eq_reference_tool curve|design|fit|check preset.txt [fits.bin]\n");
        return 2;
    }
    if (strcmp(argv[1], "curve") == 0) {
        print_curve(&curve);
    } else if (strcmp(argv[1], "design") == 0) {
        print_designs(&curve);
    } else if (strcmp(argv[1], "fit") == 0) {
        print_fits(&curve);
    } else if (strcmp(argv[1], "check") == 0 && argc == 4) {
        return check_file(&curve, argv[3]);
    } else {
        return 2;
    }
    return 0;
}

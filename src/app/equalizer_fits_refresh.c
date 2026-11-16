#include "equalizer_fits.h"

#include <stddef.h>

#include "core/string_builder.h"
#include "dsp/equalizer_fit.h"
#include "sound/equalizer_fit_file.h"

#include "equalizer_files.h"
#include "equalizer_store.h"
#include "hal/clock.h"
#include "hal/storage.h"
#include "hal/system.h"

enum {
    SLOTS = EQUALIZER_FITS_SLOTS,
    SLOT_BYTES = EQUALIZER_FITS_SLOT_BYTES,
    PATH_CAPACITY = EQUALIZER_FITS_PATH_CAPACITY,
    NAME_CAPACITY = EQUALIZER_FILES_NAME_CAPACITY,
    LINE_CAPACITY = 160,
    LOG_LIMIT = 64 * 1024,
    NO_SLOT = -1,
    WORK_ALIGNMENT = 8,
    HUNDREDTHS = 100,
    TENS = 10
};

typedef enum SlotState {
    SLOT_MISSING,
    SLOT_EMPTY,
    SLOT_DAMAGED,
    SLOT_VALID
} SlotState;

typedef struct Slot {
    uint8_t state;
    bool kept;
    uint32_t key;
} Slot;

static bool all_zero(const uint8_t *bytes, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (bytes[index] != 0) {
            return false;
        }
    }
    return true;
}

static Slot slot_at(int slot)
{
    HalFile file;
    EqualizerFitHeader header;
    if (equalizer_fits_open_slot(&file, slot, &header)) {
        hal_file_close(&file);
        return (Slot){.state = SLOT_VALID, .kept = false, .key = header.key};
    }
    char path[PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    if (!hal_file_open(&file, path)) {
        return (Slot){.state = SLOT_MISSING, .kept = false, .key = 0};
    }
    uint8_t bytes[EQUALIZER_FIT_FILE_HEADER];
    bool empty = !hal_file_read(file, 0, bytes, sizeof bytes) || all_zero(bytes, sizeof bytes);
    hal_file_close(&file);
    return (Slot){.state = empty ? SLOT_EMPTY : SLOT_DAMAGED, .kept = false, .key = 0};
}

static void read_slots(Slot *slots)
{
    for (int slot = 0; slot < SLOTS; slot++) {
        slots[slot] = slot_at(slot);
        hal_system_keep_alive();
    }
}

static int slot_with(const Slot *slots, uint32_t key)
{
    for (int slot = 0; slot < SLOTS; slot++) {
        if (slots[slot].state == SLOT_VALID && slots[slot].key == key) {
            return slot;
        }
    }
    return NO_SLOT;
}

static int free_slot(const Slot *slots)
{
    for (int slot = 0; slot < SLOTS; slot++) {
        if (!slots[slot].kept) {
            return slot;
        }
    }
    return NO_SLOT;
}

static void clear_slot(int slot, uint8_t *bytes)
{
    char path[PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    for (uint32_t index = 0; index < SLOT_BYTES; index++) {
        bytes[index] = 0;
    }
    hal_file_write(path, bytes, SLOT_BYTES);
}

static bool unused(const Slot *slot)
{
    return slot->state == SLOT_DAMAGED || (slot->state == SLOT_VALID && !slot->kept);
}

static bool any_unused(const Slot *slots)
{
    for (int slot = 0; slot < SLOTS; slot++) {
        if (unused(&slots[slot])) {
            return true;
        }
    }
    return false;
}

static void clear_unused(const Slot *slots, uint8_t *bytes)
{
    for (int slot = 0; slot < SLOTS; slot++) {
        if (unused(&slots[slot])) {
            clear_slot(slot, bytes);
        }
    }
}

static bool same_name(const char *first, const char *second)
{
    uint32_t index = 0;
    while (first[index] != '\0' && first[index] == second[index]) {
        index++;
    }
    return first[index] == second[index];
}

static bool forced(const EqualizerFitsRequest *request, const char *name)
{
    return request->all || (request->only != NULL && same_name(name, request->only));
}

static void log_path(char *path)
{
    StringBuilder builder = string_builder_make(path, PATH_CAPACITY);
    string_builder_append(&builder, EQUALIZER_FITS_FOLDER);
    string_builder_append(&builder, "/FIT.LOG");
}

typedef struct FitLog {
    bool on;
    bool known;
    bool full;
    uint32_t size;
} FitLog;

static void log_line(FitLog *log, const char *line)
{
    if (!log->on || log->full) {
        return;
    }
    char path[PATH_CAPACITY];
    log_path(path);
    if (!log->known) {
        HalFile file;
        log->size = hal_file_open(&file, path) ? file.size : 0;
        if (log->size > 0) {
            hal_file_close(&file);
        }
        log->known = true;
    }
    uint32_t length = 0;
    while (line[length] != '\0') {
        length++;
    }
    if (log->size + length > LOG_LIMIT) {
        log->full = true;
        return;
    }
    HalReserveResult room =
        log->size > 0 ? hal_file_grow(path, log->size + length) : hal_file_reserve(path, length);
    if (room != HAL_RESERVE_CREATED || !hal_file_write_at(path, log->size, line, length)) {
        log->full = true;
        return;
    }
    log->size += length;
}

static void append_hundredths(StringBuilder *text, uint32_t hundredths)
{
    string_builder_append_unsigned(text, hundredths / HUNDREDTHS);
    string_builder_append_char(text, '.');
    string_builder_append_char(text, (char)('0' + hundredths / TENS % TENS));
    string_builder_append_char(text, (char)('0' + hundredths % TENS));
}

static void log_rate(FitLog *log, uint32_t rate, const EqualizerFitStats *stats,
                     uint32_t milliseconds, bool split)
{
    char line[LINE_CAPACITY];
    StringBuilder text = string_builder_make(line, sizeof line - 1);
    string_builder_append(&text, "  ");
    string_builder_append_unsigned(&text, rate);
    string_builder_append(&text, " Hz: ");
    append_hundredths(&text, stats->fit_hundredths);
    string_builder_append(&text, " dB (usual ");
    append_hundredths(&text, stats->today_hundredths);
    string_builder_append(&text, "), ");
    string_builder_append_unsigned(&text, milliseconds);
    string_builder_append(&text, " ms, ");
    string_builder_append_unsigned(&text, stats->starts);
    string_builder_append(&text, " starts, ");
    string_builder_append_unsigned(&text, stats->rounds);
    string_builder_append(&text, " rounds, ");
    string_builder_append_unsigned(&text, hal_system_clock_mhz());
    string_builder_append(&text, split ? " MHz, core 1 helped\n" : " MHz\n");
    log_line(log, line);
}

static void log_preset(FitLog *log, const char *name, int slot, uint32_t entries, uint32_t worst,
                       uint32_t milliseconds)
{
    char path[PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    char line[LINE_CAPACITY + NAME_CAPACITY];
    StringBuilder text = string_builder_make(line, sizeof line - 1);
    string_builder_append(&text, name);
    string_builder_append(&text, " -> ");
    string_builder_append(&text, path);
    string_builder_append(&text, ": ");
    string_builder_append_unsigned(&text, entries);
    string_builder_append(&text, " rates, worst ");
    append_hundredths(&text, worst);
    string_builder_append(&text, " dB, ");
    string_builder_append_unsigned(&text, milliseconds);
    string_builder_append(&text, " ms\n");
    log_line(log, line);
}

typedef struct Step {
    EqualizerFitsProgress progress;
    uint32_t preset;
    uint32_t presets;
} Step;

static void show_step(const Step *step, uint32_t done, uint32_t steps)
{
    if (step->progress.show != NULL) {
        step->progress.show(step->progress.context, step->preset, step->presets, done, steps);
    }
}

static uint32_t rates_to_fit(const EqualizerCurve *curve)
{
    uint32_t count = 0;
    for (uint32_t rate = 0; rate < EQUALIZER_FIT_RATES; rate++) {
        count += equalizer_fit_needed(curve, EQUALIZER_FIT_RATE_LIST[rate]) ? 1u : 0u;
    }
    return count;
}

static bool better_of(EqualizerFit *best, bool have, const EqualizerFit *other)
{
    if (!have || other->worst_hundredths < best->worst_hundredths) {
        *best = *other;
    }
    return true;
}

static bool fit_rate(const EqualizerCurve *curve, uint32_t rate, void *work, uint32_t size,
                     const EqualizerFit *seed, EqualizerFit *fit, EqualizerFitStats *stats,
                     bool *split)
{
    uint32_t starts = equalizer_fit_starts(curve, rate, seed);
    *split = starts > 1 && hal_system_fit_start(curve, rate, seed, 1, 2);
    EqualizerFitPart part = {
        .part = 0, .parts = *split ? 2u : 1u, .beat = hal_system_keep_alive, .seed = seed};
    EqualizerFit best;
    bool have = equalizer_fit_part(curve, rate, work, size, part, &best, stats);
    if (*split) {
        EqualizerFit other;
        bool found = false;
        HalFitState state;
        while ((state = hal_system_fit_poll(&other, &found)) == HAL_FIT_RUNNING) {
            hal_system_keep_alive();
        }
        if (state == HAL_FIT_FAILED) {
            EqualizerFitStats rest;
            part.part = 1;
            found = equalizer_fit_part(curve, rate, work, size, part, &other, &rest);
            stats->rounds += rest.rounds;
            stats->starts += rest.starts;
            *split = false;
        }
        if (found) {
            have = better_of(&best, have, &other);
        }
    }
    if (have) {
        stats->fit_hundredths = best.worst_hundredths;
    }
    if (!have || best.worst_hundredths >= stats->today_hundredths) {
        return false;
    }
    *fit = best;
    return true;
}

typedef struct Job {
    const EqualizerCurve *curve;
    const char *name;
    uint32_t key;
    int slot;
} Job;

static bool fit_into(const Job *job, uint8_t *bytes, void *work, uint32_t size, const Step *step,
                     FitLog *log)
{
    const EqualizerCurve *curve = job->curve;
    uint32_t steps = rates_to_fit(curve);
    uint32_t done = 0;
    uint32_t entries = 0;
    uint32_t worst = 0;
    uint32_t started = hal_clock_milliseconds();
    EqualizerFit seed = {0};
    for (uint32_t rate = EQUALIZER_FIT_RATES; rate-- > 0;) {
        EqualizerFit fit;
        EqualizerFitStats stats;
        if (equalizer_fit_needed(curve, EQUALIZER_FIT_RATE_LIST[rate])) {
            show_step(step, done++, steps);
        }
        uint32_t rate_started = hal_clock_milliseconds();
        bool split = false;
        bool fitted =
            fit_rate(curve, EQUALIZER_FIT_RATE_LIST[rate], work, size, &seed, &fit, &stats, &split);
        if (stats.starts > 0) {
            log_rate(log, EQUALIZER_FIT_RATE_LIST[rate], &stats,
                     hal_clock_milliseconds() - rate_started, split);
        }
        if (!fitted) {
            continue;
        }
        seed = fit;
        equalizer_fit_file_write_entry(
            bytes + EQUALIZER_FIT_FILE_HEADER + entries * EQUALIZER_FIT_FILE_ENTRY, &fit);
        worst = fit.worst_hundredths > worst ? fit.worst_hundredths : worst;
        entries++;
    }
    show_step(step, steps, steps);
    equalizer_fit_file_write_header(
        bytes, (EqualizerFitHeader){.key = job->key,
                                    .target_rate = equalizer_target_rate(curve),
                                    .entries = entries});
    equalizer_fit_file_seal(bytes, entries);
    char path[PATH_CAPACITY];
    equalizer_fits_slot_path(path, job->slot);
    HalReserveResult reserved = hal_file_reserve(path, SLOT_BYTES);
    bool written = (reserved == HAL_RESERVE_EXISTS || reserved == HAL_RESERVE_CREATED) &&
                   hal_file_write(path, bytes, equalizer_fit_file_size(entries));
    log_preset(log, job->name, job->slot, entries, worst, hal_clock_milliseconds() - started);
    return written;
}

static uint8_t *aligned(uint8_t *memory)
{
    uintptr_t address = (uintptr_t)memory;
    return memory + ((WORK_ALIGNMENT - address % WORK_ALIGNMENT) % WORK_ALIGNMENT);
}

typedef struct Preset {
    uint32_t key;
    int16_t slot;
    bool wanted;
} Preset;

static bool fittable_curve(const char *name, EqualizerCurve *curve)
{
    EqualizerTextError error;
    return equalizer_store_file_curve(name, curve, &error) && equalizer_curve_fittable(curve);
}

static uint32_t plan(const EqualizerFitsRequest *request, Slot *slots, Preset *presets,
                     uint32_t *count, int *listed)
{
    uint32_t wanted = 0;
    *listed = 0;
    HalDir dir;
    if (!equalizer_files_open(&dir)) {
        return 0;
    }
    char name[NAME_CAPACITY];
    for (int index = 0; index < EQUALIZER_FILES_MAX && equalizer_files_next(&dir, name); index++) {
        EqualizerCurve curve;
        *listed = index + 1;
        presets[index] = (Preset){.key = 0, .slot = NO_SLOT, .wanted = false};
        hal_system_keep_alive();
        if (!fittable_curve(name, &curve)) {
            continue;
        }
        (*count)++;
        presets[index].key = equalizer_curve_key(&curve);
        int slot = slot_with(slots, presets[index].key);
        if (slot != NO_SLOT) {
            slots[slot].kept = true;
            presets[index].slot = (int16_t)slot;
        }
        if (slot == NO_SLOT || forced(request, name)) {
            presets[index].wanted = true;
            wanted++;
        }
    }
    hal_dir_close(&dir);
    return wanted;
}

static void fit_wanted(const Preset *presets, int listed, Slot *slots, const Step *base,
                       uint8_t *work, FitLog *log, EqualizerFitsReport *report)
{
    HalDir dir;
    if (!equalizer_files_open(&dir)) {
        return;
    }
    uint8_t *bytes = work + equalizer_fit_work_size();
    uint32_t done = 0;
    char name[NAME_CAPACITY];
    for (int index = 0; index < listed && equalizer_files_next(&dir, name); index++) {
        EqualizerCurve curve;
        if (!presets[index].wanted || !fittable_curve(name, &curve)) {
            continue;
        }
        Step step = {base->progress, ++done, base->presets};
        int slot = presets[index].slot != NO_SLOT ? presets[index].slot : free_slot(slots);
        if (slot == NO_SLOT) {
            report->failed++;
            continue;
        }
        slots[slot] = (Slot){.state = SLOT_VALID, .kept = true, .key = presets[index].key};
        Job job = {&curve, name, presets[index].key, slot};
        if (!fit_into(&job, bytes, work, equalizer_fit_work_size(), &step, log)) {
            slots[slot].state = SLOT_DAMAGED;
            report->failed++;
            continue;
        }
        report->fitted++;
    }
    hal_dir_close(&dir);
}

static bool same_bytes(const uint8_t *first, const uint8_t *second, uint32_t length)
{
    for (uint32_t index = 0; index < length; index++) {
        if (first[index] != second[index]) {
            return false;
        }
    }
    return true;
}

static void write_map(const Slot *slots)
{
    uint8_t map[EQUALIZER_MAP_SIZE];
    uint8_t stored[EQUALIZER_MAP_SIZE];
    equalizer_map_clear(map);
    for (int slot = 0; slot < SLOTS; slot++) {
        if (slots[slot].state == SLOT_VALID && slots[slot].kept) {
            equalizer_map_put(map, slot, slots[slot].key);
        }
    }
    equalizer_map_seal(map);
    HalFile file;
    if (!hal_file_open(&file, EQUALIZER_STORE_PATH)) {
        return;
    }
    bool room = file.size >= EQUALIZER_FILE_WITH_MAP;
    bool same = room && hal_file_read(file, EQUALIZER_MAP_OFFSET, stored, sizeof stored) &&
                same_bytes(map, stored, sizeof map);
    hal_file_close(&file);
    if (room && !same) {
        hal_file_write_at(EQUALIZER_STORE_PATH, EQUALIZER_MAP_OFFSET, map, sizeof map);
    }
}

EqualizerFitsReport equalizer_fits_refresh(EqualizerFitsProgress progress,
                                           EqualizerFitsRequest request)
{
    EqualizerFitsReport report = {0, 0, 0};
    Slot slots[SLOTS];
    Preset presets[EQUALIZER_FILES_MAX];
    int listed = 0;
    read_slots(slots);
    uint32_t wanted = plan(&request, slots, presets, &report.presets, &listed);
    if (wanted == 0 && !any_unused(slots)) {
        write_map(slots);
        return report;
    }
    uint32_t size = 0;
    uint8_t *memory = hal_system_borrow_listing(&size);
    uint32_t needed = equalizer_fit_work_size() + SLOT_BYTES + WORK_ALIGNMENT;
    if (memory == NULL || size < needed) {
        report.failed = wanted;
        write_map(slots);
        return report;
    }
    uint8_t *work = aligned(memory);
    FitLog log = {.on = request.log, .known = false, .full = false, .size = 0};
    Step base = {progress, 0, wanted};
    fit_wanted(presets, listed, slots, &base, work, &log, &report);
    clear_unused(slots, work + equalizer_fit_work_size());
    write_map(slots);
    return report;
}

const EqualizerFitsEntry EQUALIZER_FITS_ENTRY = {
    .magic = EQUALIZER_FITS_MAGIC,
    .refresh = equalizer_fits_refresh,
};

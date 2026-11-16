#include "equalizer_fits.h"

#include <stddef.h>

#include "core/resident.h"
#include "core/string_builder.h"
#include "hal/audio.h"
#include "sound/equalizer_file.h"

#include "equalizer_store.h"

enum {
    SLOTS = EQUALIZER_FITS_SLOTS,
    PATH_CAPACITY = EQUALIZER_FITS_PATH_CAPACITY
};

_Static_assert((int)EQUALIZER_FITS_SLOTS == (int)EQUALIZER_MAP_SLOTS, "the map holds every slot");

const char EQUALIZER_FITS_FOLDER[] = "/.rippl/equaliser";

RIPPL_CODE2 void equalizer_fits_slot_path(char *path, int slot)
{
    StringBuilder builder = string_builder_make(path, PATH_CAPACITY);
    string_builder_append(&builder, EQUALIZER_FITS_FOLDER);
    string_builder_append(&builder, "/E");
    string_builder_append_char(&builder, (char)('0' + (uint32_t)slot / 10 % 10));
    string_builder_append_char(&builder, (char)('0' + (uint32_t)slot % 10));
    string_builder_append(&builder, ".BIN");
}

RIPPL_CODE2 bool equalizer_fits_open_slot(HalFile *file, int slot, EqualizerFitHeader *header)
{
    char path[PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    if (!hal_file_open(file, path)) {
        return false;
    }
    uint8_t bytes[EQUALIZER_FIT_FILE_HEADER];
    uint8_t entry[EQUALIZER_FIT_FILE_ENTRY];
    bool valid = file->size >= equalizer_fit_file_size(0) &&
                 hal_file_read(*file, 0, bytes, sizeof bytes) &&
                 equalizer_fit_file_read_header(bytes, header) &&
                 file->size >= equalizer_fit_file_size(header->entries);
    uint32_t checksum =
        equalizer_fit_file_checksum(equalizer_fit_file_checksum_start(), bytes, sizeof bytes);
    for (uint32_t index = 0; valid && index < header->entries; index++) {
        valid = hal_file_read(*file, EQUALIZER_FIT_FILE_HEADER + index * EQUALIZER_FIT_FILE_ENTRY,
                              entry, sizeof entry);
        checksum = equalizer_fit_file_checksum(checksum, entry, sizeof entry);
    }
    uint8_t stored[EQUALIZER_FIT_FILE_CHECKSUM];
    valid =
        valid &&
        hal_file_read(*file, equalizer_fit_file_size(header->entries) - EQUALIZER_FIT_FILE_CHECKSUM,
                      stored, sizeof stored) &&
        equalizer_fit_file_stored_checksum(stored) == checksum;
    if (!valid) {
        hal_file_close(file);
    }
    return valid;
}

RIPPL_CODE2 bool equalizer_fits_read(uint32_t slot, uint32_t key, uint32_t rate, EqualizerFit *fit)
{
    char path[PATH_CAPACITY];
    if (slot == 0 || slot > SLOTS) {
        return false;
    }
    equalizer_fits_slot_path(path, (int)slot - 1);
    HalFile file;
    if (!hal_file_open(&file, path)) {
        return false;
    }
    uint8_t bytes[EQUALIZER_FIT_FILE_ENTRY];
    EqualizerFitHeader header;
    bool found = hal_file_read(file, 0, bytes, EQUALIZER_FIT_FILE_HEADER) &&
                 equalizer_fit_file_read_header(bytes, &header) && header.key == key;
    bool read = false;
    for (uint32_t index = 0; found && !read && index < header.entries; index++) {
        uint32_t offset = EQUALIZER_FIT_FILE_HEADER + index * EQUALIZER_FIT_FILE_ENTRY;
        found = hal_file_read(file, offset, bytes, sizeof(uint32_t));
        if (found && equalizer_fit_file_entry_rate(bytes) == rate) {
            read = hal_file_read(file, offset, bytes, sizeof bytes) &&
                   equalizer_fit_file_read_entry(bytes, header.target_rate, fit);
            found = read;
        }
    }
    hal_file_close(&file);
    return read;
}

RIPPL_CODE2 static bool digit(char character)
{
    return character >= '0' && character <= '9';
}

RIPPL_CODE2 static char upper(char character)
{
    return character >= 'a' && character <= 'z' ? (char)(character - 'a' + 'A') : character;
}

RIPPL_CODE2 static int slot_of(const char *name)
{
    bool named = upper(name[0]) == 'E' && digit(name[1]) && digit(name[2]) && name[3] == '.' &&
                 upper(name[4]) == 'B' && upper(name[5]) == 'I' && upper(name[6]) == 'N' &&
                 name[7] == '\0';
    return named ? (name[1] - '0') * 10 + (name[2] - '0') : -1;
}

RIPPL_CODE2 static bool slot_holds(int slot, uint32_t key, uint32_t target)
{
    HalFile file;
    EqualizerFitHeader header;
    if (!equalizer_fits_open_slot(&file, slot, &header)) {
        return false;
    }
    hal_file_close(&file);
    return header.key == key && header.target_rate == target;
}

RIPPL_CODE2 static int mapped_slot(uint32_t key)
{
    uint8_t map[EQUALIZER_MAP_SIZE];
    HalFile file;
    if (!hal_file_open(&file, EQUALIZER_STORE_PATH)) {
        return EQUALIZER_MAP_UNKNOWN;
    }
    bool read = file.size >= EQUALIZER_FILE_WITH_MAP &&
                hal_file_read(file, EQUALIZER_MAP_OFFSET, map, sizeof map);
    hal_file_close(&file);
    return read ? equalizer_map_find(map, key) : EQUALIZER_MAP_UNKNOWN;
}

RIPPL_CODE2 static int searched_slot(uint32_t key, uint32_t target)
{
    HalDir dir;
    int found = -1;
    if (!hal_dir_open(&dir, EQUALIZER_FITS_FOLDER)) {
        return -1;
    }
    HalDirEntry entry;
    while (found < 0 && hal_dir_next(&dir, &entry)) {
        int slot = slot_of(entry.name);
        if (slot >= 0 && !entry.directory && slot_holds(slot, key, target)) {
            found = slot;
        }
    }
    hal_dir_close(&dir);
    return found;
}

RIPPL_CODE2 uint32_t equalizer_fits_apply(const EqualizerCurve *curve)
{
    if (!equalizer_curve_fittable(curve)) {
        hal_audio_set_equalizer_fits(NULL, 0, 0);
        return 0;
    }
    uint32_t key = equalizer_curve_key(curve);
    uint32_t target = equalizer_target_rate(curve);
    int found = mapped_slot(key);
    if (found == EQUALIZER_MAP_UNKNOWN || (found >= 0 && !slot_holds(found, key, target))) {
        found = searched_slot(key, target);
    }
    if (found < 0) {
        hal_audio_set_equalizer_fits(NULL, 0, 0);
        return 0;
    }
    hal_audio_set_equalizer_fits(equalizer_fits_read, (uint32_t)found + 1, key);
    return (uint32_t)found + 1;
}

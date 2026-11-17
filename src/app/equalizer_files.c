#include "equalizer_files.h"

#include <stddef.h>

#include "core/text.h"
#include "hal/storage.h"

enum {
    EXTENSION_LENGTH = 4
};

static bool text_extension(const char *name)
{
    const char *dot = NULL;
    uint32_t length = 0;
    for (uint32_t index = 0; name[index] != '\0'; index++) {
        if (name[index] == '.') {
            dot = name + index;
        }
        length++;
    }
    return dot != NULL && length > 4 && dot[4] == '\0' && text_lower(dot[1]) == 't' &&
           text_lower(dot[2]) == 'x' && text_lower(dot[3]) == 't';
}

uint32_t equalizer_files_stem_length(const char *name)
{
    uint32_t length = 0;
    while (name[length] != '\0') {
        length++;
    }
    return text_extension(name) ? length - EXTENSION_LENGTH : length;
}

static bool preset_entry(const HalDirEntry *entry)
{
    if (entry->directory || entry->hidden || entry->name[0] == '.' ||
        !text_extension(entry->name)) {
        return false;
    }
    uint32_t length = 0;
    while (entry->name[length] != '\0') {
        length++;
    }
    return length + 1 <= EQUALIZER_PRESET_NAME_CAPACITY;
}

bool equalizer_files_open(HalDir *dir)
{
    return hal_dir_open(dir, EQUALIZER_FOLDER_PATH);
}

bool equalizer_files_next(HalDir *dir, char *name)
{
    HalDirEntry entry;
    while (hal_dir_next(dir, &entry)) {
        if (!preset_entry(&entry)) {
            continue;
        }
        uint32_t at = 0;
        for (; entry.name[at] != '\0'; at++) {
            name[at] = entry.name[at];
        }
        name[at] = '\0';
        return true;
    }
    return false;
}

int equalizer_files_find(const EqualizerFileList *list, const char *name)
{
    HalDir dir;
    if (!equalizer_files_open(&dir)) {
        return -1;
    }
    char listed[EQUALIZER_FILES_NAME_CAPACITY];
    int found = -1;
    for (int index = 0; found < 0 && index < list->count && equalizer_files_next(&dir, listed);
         index++) {
        if (text_same(listed, name)) {
            found = index;
        }
    }
    hal_dir_close(&dir);
    return found;
}

static bool nth_name(int wanted, char *name)
{
    HalDir dir;
    if (wanted < 0 || wanted >= EQUALIZER_FILES_MAX || !equalizer_files_open(&dir)) {
        return false;
    }
    bool found = false;
    for (int index = 0; !found && equalizer_files_next(&dir, name); index++) {
        found = index == wanted;
    }
    hal_dir_close(&dir);
    if (!found) {
        name[0] = '\0';
    }
    return found;
}

bool equalizer_files_name(const EqualizerFileList *list, int index, char *name)
{
    name[0] = '\0';
    return index < list->count && nth_name(index, name);
}

bool equalizer_files_stem(const EqualizerFileList *list, int index, char *stem)
{
    char name[EQUALIZER_FILES_NAME_CAPACITY];
    stem[0] = '\0';
    if (!equalizer_files_name(list, index, name)) {
        return false;
    }
    uint32_t length = equalizer_files_stem_length(name);
    for (uint32_t at = 0; at < length; at++) {
        stem[at] = name[at];
    }
    stem[length] = '\0';
    return true;
}

void equalizer_files_scan(EqualizerFileList *list)
{
    list->count = 0;
    HalDir dir;
    if (!equalizer_files_open(&dir)) {
        return;
    }
    char name[EQUALIZER_FILES_NAME_CAPACITY];
    while (list->count < EQUALIZER_FILES_MAX && equalizer_files_next(&dir, name)) {
        list->count++;
    }
    hal_dir_close(&dir);
}

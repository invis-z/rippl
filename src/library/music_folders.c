#include "library/music_folders.h"

#include <stddef.h>

#include "core/bytes.h"
#include "core/hash.h"
#include "core/text.h"
#include "core/volume_path.h"

enum {
    VERSION = 1,
    VERSION_AT = 4,
    COUNT_AT = 5,
    KEYS_AT = 8,
    CHECK_AT = KEYS_AT + 4 * MUSIC_FOLDERS_MAX
};

static const uint8_t MAGIC[4] = {'E', 'M', 'M', 'F'};
static const char INTERNAL_PREFIX[] = VOLUME_INTERNAL_NAME "/";

void music_folders_encode(const MusicFolders *folders, uint8_t *bytes)
{
    for (uint32_t index = 0; index < MUSIC_FOLDERS_FILE_BYTES; index++) {
        bytes[index] = index < sizeof MAGIC ? MAGIC[index] : 0;
    }
    bytes[VERSION_AT] = VERSION;
    bytes[COUNT_AT] = (uint8_t)folders->count;
    for (uint32_t index = 0; index < folders->count; index++) {
        bytes_put32(bytes + KEYS_AT + 4 * index, folders->keys[index]);
    }
    bytes_put32(bytes + CHECK_AT, hash_update(HASH_START, bytes, CHECK_AT));
}

bool music_folders_decode(const uint8_t *bytes, uint32_t size, MusicFolders *folders)
{
    folders->count = 0;
    if (size < MUSIC_FOLDERS_FILE_BYTES || bytes[VERSION_AT] != VERSION ||
        bytes[COUNT_AT] > MUSIC_FOLDERS_MAX ||
        bytes_get32(bytes + CHECK_AT) != hash_update(HASH_START, bytes, CHECK_AT)) {
        return false;
    }
    for (uint32_t index = 0; index < sizeof MAGIC; index++) {
        if (bytes[index] != MAGIC[index]) {
            return false;
        }
    }
    folders->count = bytes[COUNT_AT];
    for (uint32_t index = 0; index < folders->count; index++) {
        folders->keys[index] = bytes_get32(bytes + KEYS_AT + 4 * index);
    }
    return true;
}

bool music_folders_key(const char *folder, const char *name, uint32_t *key)
{
    const char *inside = volume_internal_path(folder);
    const char *top = inside != NULL ? inside : folder;
    if (top[0] != '/' || top[1] != '\0') {
        return false;
    }
    uint32_t hash = HASH_START;
    if (inside != NULL) {
        hash = hash_update(hash, INTERNAL_PREFIX, sizeof INTERNAL_PREFIX - 1);
    }
    *key = hash_update(hash, name, text_length(name));
    return true;
}

bool music_folders_has(const MusicFolders *folders, uint32_t key)
{
    for (uint32_t index = 0; index < folders->count; index++) {
        if (folders->keys[index] == key) {
            return true;
        }
    }
    return false;
}

bool music_folders_skips(const MusicFolders *folders, const char *folder, const char *name)
{
    uint32_t key = 0;
    return folders != NULL && folders->count > 0 && music_folders_key(folder, name, &key) &&
           music_folders_has(folders, key);
}

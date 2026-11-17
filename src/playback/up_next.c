#include "playback/up_next.h"

#include "core/bytes.h"
#include "core/record.h"

enum {
    VERSION = 1,
    MAGIC_FIELD = 0,
    VERSION_FIELD = 4,
    COUNT_FIELD = 6,
    IDS_FIELD = UP_NEXT_HEADER_SIZE,
    CHECKSUM_FIELD = UP_NEXT_STATE_SIZE - UP_NEXT_CHECKSUM_SIZE,
    MAGIC_SIZE = 4,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'U', 'N'};

void up_next_clear(UpNext *up_next)
{
    up_next->count = 0;
}

bool up_next_add(UpNext *up_next, uint32_t track)
{
    if (up_next->count == UP_NEXT_CAPACITY) {
        return false;
    }
    up_next->tracks[up_next->count++] = track;
    return true;
}

bool up_next_play_next(UpNext *up_next, uint32_t track)
{
    if (up_next->count == UP_NEXT_CAPACITY) {
        return false;
    }
    for (uint32_t index = up_next->count; index > 0; index--) {
        up_next->tracks[index] = up_next->tracks[index - 1];
    }
    up_next->tracks[0] = track;
    up_next->count++;
    return true;
}

bool up_next_first(const UpNext *up_next, uint32_t *track)
{
    if (up_next->count == 0) {
        return false;
    }
    *track = up_next->tracks[0];
    return true;
}

void up_next_drop(UpNext *up_next, uint32_t count)
{
    if (count >= up_next->count) {
        up_next->count = 0;
        return;
    }
    for (uint32_t index = count; index < up_next->count; index++) {
        up_next->tracks[index - count] = up_next->tracks[index];
    }
    up_next->count = up_next->count - count;
}

void up_next_encode(const UpNext *up_next, const Library *library, uint8_t *bytes)
{
    for (int index = 0; index < UP_NEXT_STATE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    bytes_put16_at(bytes, VERSION_FIELD, VERSION);
    uint32_t count = 0;
    for (uint32_t index = 0; index < up_next->count; index++) {
        uint64_t id = 0;
        if (library_track_id(library, up_next->tracks[index], &id)) {
            uint32_t field = IDS_FIELD + UP_NEXT_ID_SIZE * count++;
            bytes_put32_at(bytes, field, (uint32_t)id);
            bytes_put32_at(bytes, field + 4, (uint32_t)(id >> 32));
        }
    }
    bytes_put16_at(bytes, COUNT_FIELD, count);
    record_seal(bytes, CHECKSUM_FIELD);
}

static bool header_valid(const uint8_t *bytes)
{
    if (!record_magic_ok(bytes + MAGIC_FIELD, MAGIC, MAGIC_SIZE)) {
        return false;
    }
    return bytes_get16_at(bytes, VERSION_FIELD) == VERSION &&
           bytes_get16_at(bytes, COUNT_FIELD) <= UP_NEXT_CAPACITY &&
           record_checksum_ok(bytes, CHECKSUM_FIELD);
}

bool up_next_decode(const uint8_t *bytes, uint32_t length, const Library *library, UpNext *up_next)
{
    if (length < UP_NEXT_STATE_SIZE || !header_valid(bytes)) {
        return false;
    }
    uint32_t count = bytes_get16_at(bytes, COUNT_FIELD);
    up_next->count = 0;
    for (uint32_t index = 0; index < count; index++) {
        uint32_t field = IDS_FIELD + UP_NEXT_ID_SIZE * index;
        uint64_t id =
            (uint64_t)bytes_get32_at(bytes, field + 4) << 32 | bytes_get32_at(bytes, field);
        uint32_t track = 0;
        if (library_find_track(library, id, &track)) {
            up_next->tracks[up_next->count++] = track;
        }
    }
    return true;
}

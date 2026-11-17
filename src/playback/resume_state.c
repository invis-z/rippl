#include "playback/resume_state.h"

#include "core/bytes.h"
#include "core/record.h"

enum {
    VERSION = 1,
    MAGIC_FIELD = 0,
    VERSION_FIELD = 4,
    SHUFFLE_FIELD = 6,
    REPEAT_FIELD = 7,
    FINGERPRINT_FIELD = 8,
    SEED_FIELD = 12,
    SOURCE_KIND_FIELD = 16,
    SOURCE_GROUP_FIELD = 17,
    VOLUME_FIELD = 18,
    SOURCE_SUBJECT_FIELD = 20,
    SOURCE_ARTIST_FIELD = 24,
    ANCHOR_FIELD = 28,
    CURSOR_POSITION_FIELD = 32,
    CURSOR_ALBUM_TRACK_FIELD = 36,
    TRACK_ID_FIELD = 40,
    POSITION_FIELD = 48,
    FROM_UP_NEXT_FIELD = 52,
    CHECKSUM_FIELD = 56,
    MAGIC_SIZE = 4,
    BITS_PER_BYTE = 8
};

static const uint8_t MAGIC[MAGIC_SIZE] = {'E', 'M', 'R', 'S'};

static void encode_plan(const PlayQueuePlan *plan, uint8_t *bytes)
{
    bytes[SHUFFLE_FIELD] = (uint8_t)plan->shuffle;
    bytes_put32_at(bytes, SEED_FIELD, plan->seed);
    bytes[SOURCE_KIND_FIELD] = (uint8_t)plan->source.kind;
    bytes[SOURCE_GROUP_FIELD] = (uint8_t)plan->source.group;
    bytes_put32_at(bytes, SOURCE_SUBJECT_FIELD, plan->source.subject);
    bytes_put32_at(bytes, SOURCE_ARTIST_FIELD, plan->source.artist);
    bytes_put32_at(bytes, ANCHOR_FIELD, plan->anchor_track);
}

void resume_state_encode(const ResumeState *state, uint8_t *bytes)
{
    for (int index = 0; index < RESUME_STATE_SIZE; index++) {
        bytes[index] = index < MAGIC_SIZE ? MAGIC[index] : 0;
    }
    bytes_put16_at(bytes, VERSION_FIELD, VERSION);
    bytes[REPEAT_FIELD] = (uint8_t)state->repeat;
    bytes_put32_at(bytes, FINGERPRINT_FIELD, state->library_fingerprint);
    encode_plan(&state->plan, bytes);
    bytes_put32_at(bytes, CURSOR_POSITION_FIELD, state->cursor.position);
    bytes_put32_at(bytes, CURSOR_ALBUM_TRACK_FIELD, state->cursor.album_track);
    bytes_put32_at(bytes, TRACK_ID_FIELD, (uint32_t)state->track_id);
    bytes_put32_at(bytes, TRACK_ID_FIELD + 4, (uint32_t)(state->track_id >> 32));
    bytes[VOLUME_FIELD] = state->volume;
    bytes_put32_at(bytes, POSITION_FIELD, state->position_ms);
    bytes[FROM_UP_NEXT_FIELD] = state->from_up_next ? 1 : 0;
    record_seal(bytes, CHECKSUM_FIELD);
}

static bool header_valid(const uint8_t *bytes)
{
    if (!record_magic_ok(bytes + MAGIC_FIELD, MAGIC, MAGIC_SIZE)) {
        return false;
    }
    return bytes_get16_at(bytes, VERSION_FIELD) == VERSION &&
           record_checksum_ok(bytes, CHECKSUM_FIELD);
}

static bool group_valid(uint8_t group)
{
    return (group >= LIBRARY_ARTISTS && group <= LIBRARY_GENRES) || group == LIBRARY_PLAYLISTS;
}

static bool modes_valid(const uint8_t *bytes)
{
    return bytes[SHUFFLE_FIELD] < SHUFFLE_MODE_COUNT && bytes[REPEAT_FIELD] < REPEAT_MODE_COUNT &&
           bytes[SOURCE_KIND_FIELD] <= BROWSE_GROUP_TRACKS &&
           group_valid(bytes[SOURCE_GROUP_FIELD]);
}

static PlayQueuePlan decode_plan(const uint8_t *bytes)
{
    return (PlayQueuePlan){
        .source =
            {
                .kind = (BrowseKind)bytes[SOURCE_KIND_FIELD],
                .group = (LibrarySection)bytes[SOURCE_GROUP_FIELD],
                .subject = bytes_get32_at(bytes, SOURCE_SUBJECT_FIELD),
                .artist = bytes_get32_at(bytes, SOURCE_ARTIST_FIELD),
            },
        .anchor_track = bytes_get32_at(bytes, ANCHOR_FIELD),
        .shuffle = (ShuffleMode)bytes[SHUFFLE_FIELD],
        .seed = bytes_get32_at(bytes, SEED_FIELD),
    };
}

bool resume_state_decode(const uint8_t *bytes, uint32_t length, ResumeState *state)
{
    if (length < RESUME_STATE_SIZE || !header_valid(bytes) || !modes_valid(bytes)) {
        return false;
    }
    *state = (ResumeState){
        .library_fingerprint = bytes_get32_at(bytes, FINGERPRINT_FIELD),
        .plan = decode_plan(bytes),
        .repeat = (RepeatMode)bytes[REPEAT_FIELD],
        .volume = bytes[VOLUME_FIELD],
        .cursor = {bytes_get32_at(bytes, CURSOR_POSITION_FIELD),
                   bytes_get32_at(bytes, CURSOR_ALBUM_TRACK_FIELD)},
        .track_id = (uint64_t)bytes_get32_at(bytes, TRACK_ID_FIELD + 4) << 32 |
                    bytes_get32_at(bytes, TRACK_ID_FIELD),
        .position_ms = bytes_get32_at(bytes, POSITION_FIELD),
        .from_up_next = bytes[FROM_UP_NEXT_FIELD] != 0,
    };
    return true;
}

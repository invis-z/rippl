#include "theme_fixture.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#include "core/hash.h"
#include "fake_hal.h"

enum {
    FIXTURE_BYTES = 16384,
    FIXTURE_SLOTS = 4,
    PATH_BYTES = 512
};

static uint8_t slots[FIXTURE_SLOTS][FIXTURE_BYTES];
static int next_slot;

static uint32_t read_fixture(const char *fixture, uint8_t *bytes)
{
    const char *folders = RIPPL_THEME_SEARCH;
    while (*folders != '\0') {
        size_t length = strcspn(folders, "|");
        char path[PATH_BYTES];
        snprintf(path, sizeof path, "%.*s/%s", (int)length, folders, fixture);
        FILE *file = fopen(path, "rb");
        if (file != NULL) {
            uint32_t size = (uint32_t)fread(bytes, 1, FIXTURE_BYTES, file);
            fclose(file);
            return size;
        }
        folders += length + (folders[length] == '|');
    }
    return 0;
}

uint8_t *theme_fixture_on_card(const char *fixture, const char *card_name, uint32_t *size)
{
    char path[PATH_BYTES];
    uint8_t *bytes = slots[next_slot++ % FIXTURE_SLOTS];
    *size = read_fixture(fixture, bytes);
    snprintf(path, sizeof path, "/Themes/%s", card_name);
    fake_hal_add_file(path, bytes, *size);
    fake_hal_add_directory_entry("/Themes", card_name, false, false);
    return bytes;
}

static void put_le(uint8_t *bytes, uint32_t value, int count)
{
    for (int index = 0; index < count; index++) {
        bytes[index] = (uint8_t)(value >> (8 * index));
    }
}

enum {
    PLACED_HEADER = 52,
    PLACED_BOX = 8,
    PLACED_BYTES = PLACED_HEADER + 2 * THEME_LAYOUT_COUNT * PLACED_BOX
};

static uint8_t placed[PLACED_BYTES];

static uint8_t *put_boxes(uint8_t *at, const ThemeLayout *layout)
{
    for (int id = 0; id < THEME_LAYOUT_COUNT; id++) {
        ThemeLayoutBox box = layout->boxes[id];
        put_le(at, (uint16_t)box.x, 2);
        put_le(at + 2, (uint16_t)box.y, 2);
        put_le(at + 4, box.width, 2);
        at[6] = box.height;
        at[7] = box.align;
        at += PLACED_BOX;
    }
    return at;
}

uint8_t *theme_fixture_placed(const char *card_name, const ThemeLayout *wide,
                              const ThemeLayout *tall, uint32_t *size)
{
    char path[PATH_BYTES];
    memset(placed, 0, sizeof placed);
    memcpy(placed, "RPLT", 4);
    put_le(placed + 4, 1, 2);
    put_le(placed + 6, PLACED_HEADER, 2);
    placed[20] = 11;
    memcpy(placed + 24, "Placed", 6);
    placed[49] = wide != NULL ? THEME_LAYOUT_COUNT : 0;
    placed[50] = tall != NULL ? THEME_LAYOUT_COUNT : 0;
    uint8_t *end = placed + PLACED_HEADER;
    end = wide != NULL ? put_boxes(end, wide) : end;
    end = tall != NULL ? put_boxes(end, tall) : end;
    *size = (uint32_t)(end - placed);
    put_le(placed + 8, *size, 4);
    put_le(placed + 12, hash_update(HASH_START, placed + 16, *size - 16), 4);
    snprintf(path, sizeof path, "/Themes/%s", card_name);
    fake_hal_add_file(path, placed, *size);
    fake_hal_add_directory_entry("/Themes", card_name, false, false);
    return placed;
}

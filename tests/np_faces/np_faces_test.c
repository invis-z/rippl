#include "np_faces_test.h"

#include <stdio.h>
#include <string.h>

#include "np_faces/scene.h"
#include "tiny_font.h"
#include "ui/theme_engine.h"
#include "ui/theme_package.h"
#include "ui_test_theme.h"

enum {
    COVER_SIDE = 8,
    POSITION_MS = 139600,
    DURATION_MS = 245000,
    SCRUB_MS = 201000,
    VOLUME = 32,
    TEXT_MS = 60000,
    NOW_MS = 60600,
    TICK_MS = 100
};

const NpTestLayout NP_TEST_LAYOUTS[NP_TEST_LAYOUT_COUNT] = {
    {320, 170, {0, 0, 320, 170}},
    {170, 320, {0, 0, 170, 320}},
};

const char NP_TEST_LONG_TITLE[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                                  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                                  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
                                  "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";

static const char *const TITLES[NP_TEST_TITLE_COUNT] = {
    "Midnight Drive at the Edge of Town",
    "",
    NP_TEST_LONG_TITLE,
};

static Color cover_pixels[COVER_SIDE * COVER_SIDE];

static Image cover_image(void)
{
    for (int index = 0; index < COVER_SIDE * COVER_SIDE; index++) {
        cover_pixels[index] = 0xF800;
    }
    return (Image){cover_pixels, COVER_SIDE, COVER_SIDE};
}

static Theme scene_theme(void)
{
    Theme theme = TEST_THEME;
    theme.fonts = (ThemeFonts){&TINY_FONT, &TINY_FONT, &TINY_FONT, &TINY_FONT, &TINY_FONT};
    theme.marquee = (MarqueeTiming){.pause_ms = 1200, .speed_px_per_s = 40, .gap_px = 32};
    return theme;
}

static void apply_state(NpTestScene *scene, NpTestState state)
{
    NowPlayingFrame *frame = &scene->frame;
    bool paused = state == NP_TEST_PAUSED || state == NP_TEST_SCRUBBING || state == NP_TEST_VOLUME;
    frame->playing = !paused;
    scene->status.state = paused ? PLAYER_PAUSED : PLAYER_PLAYING;
    frame->scrubbing = state == NP_TEST_SCRUBBING;
    frame->shown_ms = state == NP_TEST_SCRUBBING ? SCRUB_MS : POSITION_MS;
    frame->volume_shown = state == NP_TEST_VOLUME;
    if (state == NP_TEST_VOLUME) {
        frame->feedback = NOW_PLAYING_FEEDBACK_VOLUME;
    }
    if (state == NP_TEST_BADGE) {
        frame->feedback = NOW_PLAYING_FEEDBACK_NEXT;
    }
    if (state == NP_TEST_NO_COVER) {
        frame->cover = (Image){0};
    }
}

static const char *const FACE_FILES[NP_FACE_COUNT] = {
    [NP_FACE_CASSETTE] = "cassette",
    [NP_FACE_TURNTABLE] = "turntable",
    [NP_FACE_TAPE_WINDOW] = "tape_window",
};

static const uint8_t *face_theme(uint8_t face, uint32_t *size)
{
    static uint8_t bytes[NP_FACE_COUNT][NP_TEST_THEME_BYTES];
    static uint32_t sizes[NP_FACE_COUNT];
    if (face <= NP_FACE_STANDARD || face >= NP_FACE_COUNT) {
        return NULL;
    }
    if (sizes[face] == 0) {
        char path[512];
        snprintf(path, sizeof path, "%s/%s.rippltheme", RIPPL_PACKED_THEMES, FACE_FILES[face]);
        FILE *file = fopen(path, "rb");
        if (file == NULL) {
            return NULL;
        }
        sizes[face] = (uint32_t)fread(bytes[face], 1, sizeof bytes[face], file);
        fclose(file);
    }
    *size = sizes[face];
    return bytes[face];
}

bool np_test_scene_code(uint8_t face, SceneCode *code, const ThemeColors **chrome)
{
    *code = (SceneCode){0};
    *chrome = NULL;
    uint32_t size = 0;
    const uint8_t *bytes = face_theme(face, &size);
    ThemePackage package = {0};
    if (bytes == NULL || !theme_package_decode(bytes, size, &package)) {
        return false;
    }
    const uint8_t *section = bytes + package.scene.offset;
    bool tinted = (package.scene.flags & THEME_SCENE_CHROME) != 0;
    *chrome = tinted ? (const ThemeColors *)(const void *)section : NULL;
    section += tinted ? THEME_SCENE_CHROME_BYTES : 0;
    static uint32_t work[SCENE_WORK_BYTES / sizeof(uint32_t)];
    *code = (SceneCode){section,
                        package.scene.code_bytes,
                        (const char *)section + package.scene.code_bytes,
                        package.scene.string_bytes,
                        work,
                        sizeof work,
                        NULL,
                        0,
                        0};
    return package.scene.code_bytes > 0;
}

NpTestScene np_test_scene(uint8_t face, NpTestLayout layout, NpTestState state)
{
    theme_engine_use(&THEME_ENGINE);
    NpTestScene scene = {.theme = scene_theme(), .layout = layout};
    scene.status = (PlayerStatus){.state = PLAYER_PLAYING,
                                  .has_track = true,
                                  .position_ms = POSITION_MS,
                                  .duration_ms = DURATION_MS,
                                  .volume = VOLUME};
    scene.frame = (NowPlayingFrame){.song = TITLES[0],
                                    .artist = "The Long Way Home",
                                    .album = "Analog Hours",
                                    .codec = "FLAC",
                                    .stream = "24-bit 96 kHz",
                                    .detail = "2,116 kbps",
                                    .modes = "Shuffle songs",
                                    .cover = cover_image(),
                                    .area = layout.area,
                                    .now_ms = NOW_MS,
                                    .text_ms = TEXT_MS,
                                    .shown_ms = POSITION_MS,
                                    .tick = POSITION_MS / TICK_MS,
                                    .playing = true};
    const ThemeColors *chrome;
    (void)np_test_scene_code(face, &scene.frame.scene, &chrome);
    apply_state(&scene, state);
    return scene;
}

NowPlayingFrame np_test_frame(const NpTestScene *scene)
{
    static uint32_t scratch[1536];
    NowPlayingFrame frame = scene->frame;
    frame.theme = &scene->theme;
    frame.status = &scene->status;
    frame.scratch = scratch;
    frame.scratch_size = (uint32_t)sizeof scratch;
    return frame;
}

void np_test_clear(Color *pixels)
{
    for (int index = 0; index < NP_TEST_PIXELS; index++) {
        pixels[index] = NP_TEST_SENTINEL;
    }
}

void np_test_draw(const NpTestScene *scene, Color *pixels)
{
    NpTestLayout layout = scene->layout;
    np_test_clear(pixels);
    Canvas canvas = canvas_make(pixels, (Rect){0, 0, layout.width, layout.height});
    NowPlayingFrame frame = np_test_frame(scene);
    NOW_PLAYING_FACES.draw(&frame, &canvas);
}

static void draw_strip(const NpTestScene *scene, Color *pixels, int top, int strip_rows)
{
    static Color strip[NP_TEST_MAX_WIDTH * NP_TEST_STRIP_ROWS];
    NpTestLayout layout = scene->layout;
    int rows = layout.height - top < strip_rows ? layout.height - top : strip_rows;
    for (int index = 0; index < layout.width * rows; index++) {
        strip[index] = NP_TEST_SENTINEL;
    }
    Canvas canvas = canvas_make(strip, (Rect){0, top, layout.width, rows});
    NowPlayingFrame frame = np_test_frame(scene);
    NOW_PLAYING_FACES.draw(&frame, &canvas);
    memcpy(pixels + top * layout.width, strip, (size_t)(layout.width * rows) * sizeof strip[0]);
}

void np_test_draw_strips_of(const NpTestScene *scene, Color *pixels, int strip_rows)
{
    for (int top = 0; top < scene->layout.height; top += strip_rows) {
        draw_strip(scene, pixels, top, strip_rows);
    }
}

void np_test_draw_strips(const NpTestScene *scene, Color *pixels)
{
    np_test_draw_strips_of(scene, pixels, NP_TEST_STRIP_ROWS);
}

bool np_test_same(const Color *first, const Color *second, NpTestLayout layout)
{
    return memcmp(first, second, (size_t)(layout.width * layout.height) * sizeof first[0]) == 0;
}

Color np_test_pixel(const Color *pixels, NpTestLayout layout, int x, int y)
{
    return pixels[y * layout.width + x];
}

bool np_test_area_painted(const Color *pixels, NpTestLayout layout)
{
    for (int y = layout.area.y; y < layout.area.y + layout.area.height; y++) {
        for (int x = layout.area.x; x < layout.area.x + layout.area.width; x++) {
            if (np_test_pixel(pixels, layout, x, y) == NP_TEST_SENTINEL) {
                return false;
            }
        }
    }
    return true;
}

bool np_test_outside_untouched(const Color *pixels, NpTestLayout layout)
{
    for (int y = 0; y < layout.height; y++) {
        bool inside_rows = y >= layout.area.y && y < layout.area.y + layout.area.height;
        for (int x = 0; x < layout.width; x++) {
            bool inside =
                inside_rows && x >= layout.area.x && x < layout.area.x + layout.area.width;
            if (!inside && np_test_pixel(pixels, layout, x, y) != NP_TEST_SENTINEL) {
                return false;
            }
        }
    }
    return true;
}

bool np_test_for_each_scene(NpTestCheck check)
{
    bool all = true;
    for (int face = NP_FACE_CASSETTE; face < NP_FACE_COUNT; face++) {
        for (int layout = 0; layout < NP_TEST_LAYOUT_COUNT; layout++) {
            for (int state = 0; state < NP_TEST_STATE_COUNT; state++) {
                for (int title = 0; title < NP_TEST_TITLE_COUNT; title++) {
                    NpTestScene scene =
                        np_test_scene((uint8_t)face, NP_TEST_LAYOUTS[layout], (NpTestState)state);
                    scene.frame.song = TITLES[title];
                    if (!check(&scene)) {
                        fprintf(stderr, "face %d layout %d state %d title %d\n", face, layout,
                                state, title);
                        all = false;
                    }
                }
            }
        }
    }
    return all;
}

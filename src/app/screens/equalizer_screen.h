#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "equalizer_files.h"
#include "equalizer_store.h"
#include "ui/list_view.h"
#include "ui/menu_screen.h"
#include "ui/screen.h"
#include "ui/theme.h"
#include "ui/toast.h"

enum {
    EQUALIZER_NOTICE_CAPACITY = 48,
    EQUALIZER_NAME_CACHE = 8,
    EQUALIZER_ACTIONS = 2
};

typedef struct EqualizerNameCache {
    int files[EQUALIZER_NAME_CACHE];
    char stems[EQUALIZER_NAME_CACHE][EQUALIZER_PRESET_NAME_CAPACITY];
    uint32_t next;
} EqualizerNameCache;

typedef struct EqualizerConfig {
    EqualizerStore *store;
    Toast *toast;
    const Theme *theme;
    bool (*refit)(const char *name);
} EqualizerConfig;

typedef struct EqualizerScreen {
    Screen screen;
    EqualizerConfig config;
    ListModel model;
    ListView list;
    uint32_t now_ms;
    EqualizerFileList files;
    int file_preview;
    int chosen_file;
    char notice[EQUALIZER_NOTICE_CAPACITY];
    EqualizerNameCache names;
    MenuScreen actions;
    MenuItem action_items[EQUALIZER_ACTIONS];
    char action_title[EQUALIZER_PRESET_NAME_CAPACITY];
    int action_file;
} EqualizerScreen;

Screen *equalizer_screen_init(EqualizerScreen *equalizer, EqualizerConfig config);
void equalizer_screen_refresh(EqualizerScreen *equalizer);
bool equalizer_screen_has_presets(const void *context);
ScreenAction equalizer_screen_open(void *context);
const char *equalizer_screen_summary(const void *context);
const char *equalizer_screen_preset_name(EqualizerPreset preset);

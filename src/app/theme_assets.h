#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "hal/storage.h"
#include "ui/theme_package.h"

typedef struct ThemeSource {
    HalFile file;
    uint32_t size;
    int builtin;
} ThemeSource;

typedef struct ThemeAssets {
    Wallpaper wallpaper;
    uint32_t wipes;
    uint8_t wallpaper_state;
    uint8_t wallpaper_kind;
} ThemeAssets;

void theme_assets_reset(ThemeAssets *assets);
const Wallpaper *theme_assets_wallpaper(ThemeAssets *assets, const ThemePackage *package,
                                        bool portrait);
void theme_assets_watch(ThemeAssets *assets);
bool theme_assets_wanted(const ThemeAssets *assets);
bool theme_assets_tend(ThemeAssets *assets, const ThemePackage *package, const ThemeSource *source);
void *theme_assets_spare(const ThemePackage *package, uint32_t *size);
bool theme_source_read(const ThemeSource *source, uint32_t offset, void *target, uint32_t length);

#include "theme_assets.h"

#include <stddef.h>

#include "core/hash.h"
#include "core/resident.h"
#include "hal/system.h"

enum {
    WALLPAPER_PAIR_BYTES = 1024
};

typedef enum AssetState {
    ASSET_NONE,
    ASSET_WANTED,
    ASSET_READY,
    ASSET_FAILED
} AssetState;

typedef struct Arena {
    uint8_t *bytes;
    uint32_t size;
} Arena;

_Static_assert(HAL_THEME_ASSET_BYTES >= WALLPAPER_PAIR_BYTES + THEME_WALLPAPER_LONG_SIDE *
                                                                   THEME_WALLPAPER_SHORT_SIDE / 2,
               "the asset memory holds a whole screen of wallpaper");

RIPPL_STARTUP_CODE void theme_assets_reset(ThemeAssets *assets)
{
    assets->wallpaper_state = ASSET_NONE;
    assets->wallpaper.ready = false;
}

static bool small_enough_to_turn(ThemePackageWallpaper wallpaper)
{
    return wallpaper.width > 0 && wallpaper.width <= THEME_WALLPAPER_SHORT_SIDE &&
           wallpaper.height <= THEME_WALLPAPER_SHORT_SIDE;
}

const Wallpaper *theme_assets_wallpaper(ThemeAssets *assets, const ThemePackage *package,
                                        bool portrait)
{
    uint8_t kind = portrait ? THEME_WALLPAPER_PORTRAIT : THEME_WALLPAPER_LANDSCAPE;
    if (package->wallpapers[kind].width == 0) {
        kind = (uint8_t)(THEME_WALLPAPER_PORTRAIT - kind);
        if (!small_enough_to_turn(package->wallpapers[kind])) {
            return NULL;
        }
    }
    if (assets->wallpaper_state == ASSET_NONE || assets->wallpaper_kind != kind) {
        assets->wallpaper_kind = kind;
        assets->wallpaper_state = ASSET_WANTED;
        assets->wallpaper.ready = false;
    }
    return &assets->wallpaper;
}

void theme_assets_watch(ThemeAssets *assets)
{
    if (assets->wipes == hal_system_theme_asset_wipes()) {
        return;
    }
    if (assets->wallpaper_state == ASSET_READY) {
        assets->wallpaper_state = ASSET_WANTED;
        assets->wallpaper.ready = false;
    }
}

bool theme_assets_wanted(const ThemeAssets *assets)
{
    return assets->wallpaper_state == ASSET_WANTED;
}

bool theme_source_read(const ThemeSource *source, uint32_t offset, void *target, uint32_t length)
{
    if (offset > source->size || length > source->size - offset) {
        return false;
    }
    return source->builtin >= 0
               ? hal_system_builtin_theme_read((uint32_t)source->builtin, offset, target, length)
               : hal_file_read(source->file, offset, target, length);
}

RIPPL_STARTUP_HELPER static void fill_pairs(uint32_t *pairs, const uint8_t *palette)
{
    for (uint32_t pair = 0; pair < WALLPAPER_PAIR_BYTES / sizeof(uint32_t); pair++) {
        const uint8_t *left = palette + 2 * (pair >> 4);
        const uint8_t *right = palette + 2 * (pair & 0x0F);
        pairs[pair] = (uint32_t)left[0] | (uint32_t)left[1] << 8 | (uint32_t)right[0] << 16 |
                      (uint32_t)right[1] << 24;
    }
}

static bool read_wallpaper(ThemeAssets *assets, ThemePackageWallpaper entry,
                           const ThemeSource *file, Arena arena)
{
    uint8_t palette[THEME_PACKAGE_PALETTE_BYTES];
    uint32_t bytes = entry.width / 2u * entry.height;
    uint32_t *pairs = (uint32_t *)(void *)arena.bytes;
    uint8_t *rows = arena.bytes + WALLPAPER_PAIR_BYTES;
    if (WALLPAPER_PAIR_BYTES + bytes > arena.size ||
        !theme_source_read(file, entry.offset, palette, sizeof palette) ||
        !theme_source_read(file, entry.offset + (uint32_t)sizeof palette, rows, bytes) ||
        hash_update(hash_update(HASH_START, palette, sizeof palette), rows, bytes) !=
            entry.checksum) {
        return false;
    }
    fill_pairs(pairs, palette);
    assets->wallpaper = (Wallpaper){pairs, rows, entry.width, entry.height, true};
    return true;
}

static void load_wallpaper(ThemeAssets *assets, const ThemePackage *package,
                           const ThemeSource *file, Arena arena)
{
    ThemePackageWallpaper entry = package->wallpapers[assets->wallpaper_kind];
    bool loaded = read_wallpaper(assets, entry, file, arena);
    assets->wallpaper_state = loaded ? ASSET_READY : ASSET_FAILED;
}

static uint32_t wallpaper_bytes(ThemePackageWallpaper entry)
{
    if (entry.width == 0) {
        return 0;
    }
    uint32_t bytes = WALLPAPER_PAIR_BYTES + entry.width / 2u * entry.height;
    return (bytes + 3u) & ~3u;
}

void *theme_assets_spare(const ThemePackage *package, uint32_t *size)
{
    uint32_t total = 0;
    uint8_t *memory = hal_system_theme_assets(&total);
    uint32_t landscape = wallpaper_bytes(package->wallpapers[THEME_WALLPAPER_LANDSCAPE]);
    uint32_t portrait = wallpaper_bytes(package->wallpapers[THEME_WALLPAPER_PORTRAIT]);
    uint32_t used = landscape > portrait ? landscape : portrait;
    if (memory == NULL || used >= total) {
        *size = 0;
        return NULL;
    }
    *size = total - used;
    return memory + used;
}

RIPPL_STARTUP_CODE bool theme_assets_tend(ThemeAssets *assets, const ThemePackage *package,
                                          const ThemeSource *file)
{
    uint32_t size = 0;
    void *memory = hal_system_theme_assets(&size);
    Arena arena = {memory, memory != NULL ? size : 0};
    assets->wipes = hal_system_theme_asset_wipes();
    if (assets->wallpaper_state == ASSET_WANTED) {
        load_wallpaper(assets, package, file, arena);
    }
    return true;
}

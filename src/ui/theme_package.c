#include "ui/theme_package.h"

#include "core/bytes.h"
#include "core/hash.h"
#include "core/resident.h"

enum {
    VERSION_FIELD = 4,
    HEADER_BYTES_FIELD = 6,
    TOTAL_BYTES_FIELD = 8,
    CHECKSUM_FIELD = 12,
    CHECKED_FROM = 16,
    COLOR_BYTES = 2
};

static const uint8_t MAGIC[] = {'R', 'P', 'L', 'T'};

static const uint8_t SPACING_RANGES[THEME_SPACING_COUNT][2] = {
#define THEME_SPACING(id, name, least, most) {least, most},
#include "ui/theme_spacing.def"
#undef THEME_SPACING
};

static const uint8_t ICON_LIMITS[THEME_ICON_COUNT][2] = {
#define THEME_ICON(id, name, width, height) {width, height},
#include "ui/theme_icons.def"
#undef THEME_ICON
};

RIPPL_STARTUP_HELPER static uint32_t checked_size(const uint8_t *bytes, uint32_t size)
{
    if (size < THEME_PACKAGE_HEADER_BYTES) {
        return 0;
    }
    for (uint32_t index = 0; index < sizeof MAGIC; index++) {
        if (bytes[index] != MAGIC[index]) {
            return 0;
        }
    }
    uint32_t held = bytes_get32_at(bytes, TOTAL_BYTES_FIELD);
    bool sound = bytes_get16_at(bytes, VERSION_FIELD) == THEME_PACKAGE_VERSION &&
                 bytes_get16_at(bytes, HEADER_BYTES_FIELD) >= THEME_PACKAGE_HEADER_BYTES &&
                 held >= THEME_PACKAGE_HEADER_BYTES && held <= size &&
                 held <= THEME_PACKAGE_MAX_BYTES &&
                 bytes_get32_at(bytes, CHECKSUM_FIELD) ==
                     hash_update(HASH_START, bytes + CHECKED_FROM, held - CHECKED_FROM);
    return sound ? held : 0;
}

RIPPL_STARTUP_HELPER static bool wallpaper_fits(const uint8_t *entry, uint32_t held)
{
    bool portrait = entry[0] == THEME_WALLPAPER_PORTRAIT;
    uint32_t width = bytes_get16_at(entry, 2);
    uint32_t height = bytes_get16_at(entry, 4);
    uint32_t widest = portrait ? THEME_WALLPAPER_SHORT_SIDE : THEME_WALLPAPER_LONG_SIDE;
    uint32_t tallest = portrait ? THEME_WALLPAPER_LONG_SIDE : THEME_WALLPAPER_SHORT_SIDE;
    return entry[0] < THEME_WALLPAPER_COUNT && width >= 2 && width % 2 == 0 && width <= widest &&
           height >= 1 && height <= tallest && bytes_get32_at(entry, 8) >= held;
}

RIPPL_STARTUP_HELPER static bool scene_fits(const uint8_t *entry, uint32_t held)
{
    uint32_t chrome = (entry[1] & THEME_SCENE_CHROME) != 0 ? THEME_SCENE_CHROME_BYTES : 0;
    uint32_t bytes = chrome + bytes_get16_at(entry, 2) + bytes_get16_at(entry, 4);
    bool scene = entry[0] == THEME_ASSET_SCENE || entry[0] == THEME_ASSET_HOME_SCENE;
    return scene && bytes_get16_at(entry, 2) >= 1 && bytes <= THEME_SCENE_MOST_BYTES &&
           bytes_get32_at(entry, 8) >= held;
}

RIPPL_STARTUP_HELPER static void take_asset(const uint8_t *entry, uint32_t held,
                                            ThemePackage *package)
{
    if (scene_fits(entry, held)) {
        ThemePackageScene *slot =
            entry[0] == THEME_ASSET_HOME_SCENE ? &package->home_scene : &package->scene;
        *slot = (ThemePackageScene){bytes_get32_at(entry, 8), bytes_get32_at(entry, 12),
                                    (uint16_t)bytes_get16_at(entry, 2),
                                    (uint16_t)bytes_get16_at(entry, 4), entry[1]};
        return;
    }
    if (wallpaper_fits(entry, held)) {
        package->wallpapers[entry[0]] = (ThemePackageWallpaper){
            bytes_get32_at(entry, 8), bytes_get32_at(entry, 12), (uint16_t)bytes_get16_at(entry, 2),
            (uint16_t)bytes_get16_at(entry, 4)};
    }
}

RIPPL_STARTUP_HELPER static void take_assets(const uint8_t *table, uint32_t count, uint32_t held,
                                             ThemePackage *package)
{
    for (uint32_t kind = 0; kind < THEME_WALLPAPER_COUNT; kind++) {
        package->wallpapers[kind] = (ThemePackageWallpaper){0};
    }
    package->scene = (ThemePackageScene){0};
    package->home_scene = (ThemePackageScene){0};
    for (uint32_t index = 0; index < count; index++) {
        take_asset(table + index * THEME_PACKAGE_ASSET_ENTRY_BYTES, held, package);
    }
}

RIPPL_STARTUP_HELPER static bool box_usable(const BoxStyle *box)
{
    return box->fill_top < THEME_COLOR_COUNT && box->fill_bottom < THEME_COLOR_COUNT &&
           box->border < THEME_COLOR_COUNT && box->shine < THEME_COLOR_COUNT &&
           box->ink < THEME_COLOR_COUNT && box->border_edges <= BOX_EDGE_ALL &&
           box->see_through <= COLOR_ALPHA_OPAQUE;
}

RIPPL_STARTUP_HELPER static void take_colors(const uint8_t *bytes, uint32_t count,
                                             ThemeColors *colors)
{
    Color *table = (Color *)colors;
    uint32_t known = count < THEME_COLOR_COUNT ? count : THEME_COLOR_COUNT;
    for (uint32_t index = 0; index < known; index++) {
        table[index] = (Color)bytes_get16_at(bytes, index * COLOR_BYTES);
    }
}

RIPPL_STARTUP_HELPER static void take_box(const uint8_t *bytes, uint32_t box_bytes, BoxStyle *box)
{
    BoxStyle taken = *box;
    uint8_t *fields = (uint8_t *)&taken;
    uint32_t known = box_bytes < sizeof taken ? box_bytes : sizeof taken;
    for (uint32_t index = 0; index < known; index++) {
        fields[index] = bytes[index];
    }
    if (box_usable(&taken)) {
        *box = taken;
    }
}

RIPPL_STARTUP_HELPER static void take_boxes(const uint8_t *bytes, uint32_t count,
                                            uint32_t box_bytes, ThemeBoxes *boxes)
{
    BoxStyle *table = (BoxStyle *)boxes;
    uint32_t known = count < THEME_BOX_COUNT ? count : THEME_BOX_COUNT;
    for (uint32_t index = 0; index < known; index++) {
        take_box(bytes + index * box_bytes, box_bytes, &table[index]);
    }
}

RIPPL_STARTUP_HELPER static void take_spacing(const uint8_t *bytes, uint32_t count,
                                              uint8_t *spacing)
{
    for (uint32_t index = 0; index < THEME_SPACING_COUNT; index++) {
        uint32_t stored = index < count ? bytes[index] : THEME_PACKAGE_SPACING_UNSET;
        bool usable = stored > SPACING_RANGES[index][0] && stored <= SPACING_RANGES[index][1] + 1u;
        spacing[index] = usable ? (uint8_t)stored : THEME_PACKAGE_SPACING_UNSET;
    }
}

RIPPL_STARTUP_HELPER static bool icon_fits(const uint8_t *entry, uint32_t id, uint32_t room)
{
    uint32_t width = entry[0];
    uint32_t height = entry[1];
    uint32_t offset = bytes_get16_at(entry, 2);
    return width > 0 && height > 0 && width <= ICON_LIMITS[id][0] && height <= ICON_LIMITS[id][1] &&
           offset <= room && (width + 1) / 2 * height <= room - offset;
}

RIPPL_STARTUP_HELPER static void take_icons(const uint8_t *bytes, uint32_t size, uint32_t table_at,
                                            ThemePackageIcon *icons)
{
    uint32_t count = bytes[THEME_PACKAGE_ICONS_FIELD];
    uint32_t data_at = table_at + count * THEME_PACKAGE_ICON_ENTRY_BYTES;
    for (uint32_t id = 0; id < THEME_ICON_COUNT; id++) {
        const uint8_t *entry = bytes + table_at + id * THEME_PACKAGE_ICON_ENTRY_BYTES;
        icons[id] = (ThemePackageIcon){0};
        if (id < count && icon_fits(entry, id, size - data_at)) {
            icons[id] = (ThemePackageIcon){(uint16_t)(data_at + bytes_get16_at(entry, 2)), entry[0],
                                           entry[1]};
        }
    }
}

RIPPL_STARTUP_HELPER static bool box_sound(ThemeLayoutBox box, uint32_t id)
{
    int reach = THEME_LAYOUT_REACH;
    int widest = id == THEME_LAYOUT_COVER ? THEME_LAYOUT_MOST_COVER : THEME_LAYOUT_REACH;
    return box.align < THEME_ALIGN_COUNT && box.x >= -reach && box.x <= reach && box.y >= -reach &&
           box.y <= reach && box.width <= widest;
}

RIPPL_STARTUP_HELPER static void take_layout(const uint8_t *bytes, uint32_t count,
                                             ThemeLayout *layout)
{
    for (uint32_t id = 0; id < THEME_LAYOUT_COUNT; id++) {
        const uint8_t *field = bytes + id * THEME_PACKAGE_LAYOUT_BOX_BYTES;
        ThemeLayoutBox box = {.align = THEME_ALIGN_UNSET};
        if (id < count) {
            box = (ThemeLayoutBox){(int16_t)bytes_get16_at(field, 0),
                                   (int16_t)bytes_get16_at(field, 2),
                                   (uint16_t)bytes_get16_at(field, 4), field[6], field[7]};
        }
        layout->boxes[id] = box_sound(box, id) ? box : (ThemeLayoutBox){.align = THEME_ALIGN_UNSET};
    }
}

RIPPL_STARTUP_HELPER static void take_accents(const uint8_t *bytes, uint32_t count,
                                              ThemePackage *package)
{
    uint32_t known = count < THEME_PACKAGE_MOST_ACCENTS ? count : THEME_PACKAGE_MOST_ACCENTS;
    for (uint32_t index = 0; index < known; index++) {
        const uint8_t *entry = bytes + index * THEME_PACKAGE_ACCENT_BYTES;
        package->accents[index] =
            (ThemeAccent){(Color)bytes_get16_at(entry, 0), (Color)bytes_get16_at(entry, 2),
                          (Color)bytes_get16_at(entry, 4), entry[6]};
    }
    package->accent_count = (uint8_t)known;
}

RIPPL_STARTUP_HELPER static void take_name(const uint8_t *bytes, char *name)
{
    for (uint32_t index = 0; index + 1 < THEME_PACKAGE_NAME_BYTES; index++) {
        name[index] = (char)bytes[index];
    }
    name[THEME_PACKAGE_NAME_BYTES - 1] = '\0';
}

RIPPL_STARTUP_CODE bool theme_package_decode(const uint8_t *bytes, uint32_t size,
                                             ThemePackage *package)
{
    size = checked_size(bytes, size);
    if (size == 0) {
        return false;
    }
    uint32_t header = bytes_get16_at(bytes, HEADER_BYTES_FIELD);
    uint32_t colors = bytes_get16_at(bytes, THEME_PACKAGE_COLORS_FIELD);
    uint32_t boxes = bytes_get16_at(bytes, THEME_PACKAGE_BOXES_FIELD);
    uint32_t box_bytes = bytes[THEME_PACKAGE_BOX_BYTES_FIELD];
    uint32_t spacing = bytes[THEME_PACKAGE_SPACING_FIELD];
    uint32_t boxes_at = header + colors * COLOR_BYTES;
    if (header > size || boxes_at > size || boxes * box_bytes > size - boxes_at ||
        spacing > size - boxes_at - boxes * box_bytes) {
        return false;
    }
    bool long_header = header >= THEME_PACKAGE_ASSET_HEADER_BYTES;
    uint32_t layouts_at = boxes_at + boxes * box_bytes + spacing;
    uint32_t wide = long_header ? bytes[THEME_PACKAGE_LAYOUTS_FIELD] : 0;
    uint32_t tall = long_header ? bytes[THEME_PACKAGE_LAYOUTS_FIELD + 1] : 0;
    uint32_t layout_bytes = (wide + tall) * THEME_PACKAGE_LAYOUT_BOX_BYTES;
    if (layout_bytes > size - layouts_at) {
        return false;
    }
    uint32_t accents_at = layouts_at + layout_bytes;
    uint32_t accents = long_header ? bytes[THEME_PACKAGE_ACCENTS_FIELD] : 0;
    if (accents * THEME_PACKAGE_ACCENT_BYTES > size - accents_at) {
        return false;
    }
    uint32_t assets_at = accents_at + accents * THEME_PACKAGE_ACCENT_BYTES;
    uint32_t assets = long_header ? bytes[THEME_PACKAGE_ASSETS_FIELD] : 0;
    if (assets * THEME_PACKAGE_ASSET_ENTRY_BYTES > size - assets_at) {
        return false;
    }
    uint32_t icons_at = assets_at + assets * THEME_PACKAGE_ASSET_ENTRY_BYTES;
    if (bytes[THEME_PACKAGE_ICONS_FIELD] * THEME_PACKAGE_ICON_ENTRY_BYTES > size - icons_at) {
        return false;
    }
    take_assets(bytes + assets_at, assets, size, package);
    take_layout(bytes + layouts_at, wide, &package->layouts[THEME_WALLPAPER_LANDSCAPE]);
    take_layout(bytes + layouts_at + wide * THEME_PACKAGE_LAYOUT_BOX_BYTES, tall,
                &package->layouts[THEME_WALLPAPER_PORTRAIT]);
    take_icons(bytes, size, icons_at, package->icons);
    take_accents(bytes + accents_at, accents, package);
    package->accents_at = (uint16_t)accents_at;
    package->flags =
        header >= THEME_PACKAGE_FLAGS_HEADER_BYTES ? bytes[THEME_PACKAGE_FLAGS_FIELD] : 0;
    take_colors(bytes + header, colors, &package->colors);
    take_boxes(bytes + boxes_at, boxes, box_bytes, &package->boxes);
    take_spacing(bytes + boxes_at + boxes * box_bytes, spacing, package->spacing);
    take_name(bytes + THEME_PACKAGE_NAME_FIELD, package->name);
    uint8_t selection = bytes[THEME_PACKAGE_SELECTION_FIELD];
    package->selection = selection < THEME_BOX_COUNT ? selection : THEME_BOX_SELECTION;
    return true;
}

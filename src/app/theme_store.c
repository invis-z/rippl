#include "theme_store.h"

#include <stddef.h>

#include "app/startup.h"
#include "core/hash.h"
#include "core/resident.h"
#include "core/string_builder.h"
#include "core/text.h"
#include "default_theme.h"
#include "hal/storage.h"
#include "hal/system.h"
#include "ui/theme_engine.h"

enum {
    MAGIC_BYTES = 4,
    RECORD_BYTES = MAGIC_BYTES + THEME_PACKAGE_NAME_BYTES,
    PATH_CAPACITY = 64,
    SUFFIX_BYTES = 11,
    CASE_BIT = 0x20,
    NO_BUILTIN = -1
};

const char THEME_STORE_PATH[] = "/.rippl/theme.bin";
const char THEME_STORE_FOLDER[] = "/Themes";

static const char SUFFIX[SUFFIX_BYTES + 1] = ".rippltheme";
static const uint8_t MAGIC[MAGIC_BYTES] = {'R', 'P', 'L', 'C'};

typedef struct ThemeMemory {
    ThemePackage package;
    bool active;
    bool card;
    int8_t builtin;
    uint8_t revision;
    uint8_t count;
    char names[THEME_STORE_FILES][THEME_PACKAGE_NAME_BYTES];
    const char *options[THEME_STORE_OPTIONS];
    char accent_name[THEME_PACKAGE_NAME_BYTES];
    ThemeIcons icons;
    ThemeAssets assets;
    uint32_t file_wipes;
    bool file_ready;
} ThemeMemory;

_Static_assert(sizeof(ThemeMemory) <= HAL_THEME_MEMORY_BYTES,
               "the theme memory the platform hands over holds the store");

_Static_assert((int)HAL_THEME_FILE_BYTES >= (int)THEME_PACKAGE_MAX_BYTES,
               "a theme's head fits its place in High RAM");

static uint8_t *file_bytes(void)
{
    uint32_t size = 0;
    uint8_t *bytes = hal_system_theme_file(&size);
    return size >= THEME_PACKAGE_MAX_BYTES ? bytes : NULL;
}

static ThemeMemory *memory(void)
{
    uint32_t size = 0;
    ThemeMemory *block = hal_system_theme_memory(&size);
    return size >= sizeof(ThemeMemory) ? block : NULL;
}

static void copy_name(char *target, const char *source, size_t length)
{
    bool ended = false;
    for (size_t index = 0; index < THEME_PACKAGE_NAME_BYTES; index++) {
        ended = ended || index >= length || source[index] == '\0';
        target[index] = ended ? '\0' : source[index];
    }
}

RIPPL_STARTUP_HELPER static size_t stem_length(const char *file)
{
    size_t length = 0;
    while (file[length] != '\0') {
        length++;
    }
    if (length <= SUFFIX_BYTES || length - SUFFIX_BYTES >= THEME_PACKAGE_NAME_BYTES) {
        return 0;
    }
    for (size_t index = 0; index < SUFFIX_BYTES; index++) {
        char letter = file[length - SUFFIX_BYTES + index];
        if ((letter | (index > 0 ? CASE_BIT : 0)) != SUFFIX[index]) {
            return 0;
        }
    }
    return length - SUFFIX_BYTES;
}

RIPPL_STARTUP_HELPER static bool decode(ThemeMemory *block, uint32_t size)
{
    ThemePackage decoded = {.colors = DEFAULT_THEME_DARK_COLORS, .boxes = THEME_STANDARD_BOXES};
    uint8_t *file = file_bytes();
    if (file == NULL || !theme_package_decode(file, size, &decoded)) {
        return false;
    }
    for (int id = 0; id < THEME_ICON_COUNT; id++) {
        ThemePackageIcon icon = decoded.icons[id];
        const uint8_t *levels = icon.width > 0 ? file + icon.offset : NULL;
        block->icons.masks[id] = (IconMask){levels, icon.width, icon.height};
    }
    block->package = decoded;
    block->file_wipes = hal_system_theme_asset_wipes();
    block->file_ready = true;
    return true;
}

RIPPL_STARTUP_HELPER static bool open_card(const char *name, ThemeSource *source)
{
    char path[PATH_CAPACITY];
    StringBuilder builder = string_builder_make(path, sizeof path);
    string_builder_append(&builder, THEME_STORE_FOLDER);
    string_builder_append_char(&builder, '/');
    string_builder_append(&builder, name);
    string_builder_append(&builder, SUFFIX);
    source->builtin = NO_BUILTIN;
    if (!hal_file_open(&source->file, path)) {
        return false;
    }
    source->size = source->file.size;
    return true;
}

RIPPL_STARTUP_HELPER static bool open_builtin(int theme, ThemeSource *source)
{
    source->builtin = theme;
    source->size = theme >= 0 ? hal_system_builtin_theme_size((uint32_t)theme) : 0;
    return source->size > 0;
}

RIPPL_STARTUP_HELPER static void close_source(ThemeSource *source)
{
    if (source->builtin == NO_BUILTIN) {
        hal_file_close(&source->file);
    }
}

RIPPL_STARTUP_HELPER static bool open_worn(const ThemeMemory *block, ThemeSource *source)
{
    return block->card ? open_card(block->package.name, source)
                       : open_builtin(block->builtin, source);
}

RIPPL_STARTUP_HELPER static bool read_package(ThemeMemory *block, ThemeSource *source)
{
    uint8_t *file = file_bytes();
    uint32_t held = source->size < THEME_PACKAGE_MAX_BYTES ? source->size : THEME_PACKAGE_MAX_BYTES;
    bool read = file != NULL && theme_source_read(source, 0, file, held);
    close_source(source);
    return read && decode(block, held);
}

RIPPL_STARTUP_HELPER static bool read_card(ThemeMemory *block, const char *name)
{
    ThemeSource source;
    if (name[0] == '\0' || !open_card(name, &source) || !read_package(block, &source)) {
        return false;
    }
    copy_name(block->package.name, name, THEME_PACKAGE_NAME_BYTES - 1);
    return true;
}

RIPPL_STARTUP_HELPER static bool read_builtin(ThemeMemory *block)
{
    ThemeSource source;
    return open_builtin(block->builtin, &source) && read_package(block, &source);
}

RIPPL_STARTUP_HELPER static void apply(ThemeMemory *block, const char *card)
{
    block->card = read_card(block, card);
    block->active = block->card || read_builtin(block);
    theme_assets_reset(&block->assets);
    block->revision++;
}

RIPPL_STARTUP_HELPER static void remember(const ThemeMemory *block)
{
    uint8_t record[RECORD_BYTES] = {0};
    for (int index = 0; index < MAGIC_BYTES; index++) {
        record[index] = MAGIC[index];
    }
    if (block->card) {
        copy_name((char *)record + MAGIC_BYTES, block->package.name, THEME_PACKAGE_NAME_BYTES - 1);
    }
    (void)hal_file_write(THEME_STORE_PATH, record, sizeof record);
}

RIPPL_STARTUP_HELPER static bool remembered(char *name)
{
    uint8_t record[RECORD_BYTES];
    HalFile file;
    if (!hal_file_open(&file, THEME_STORE_PATH)) {
        return false;
    }
    bool read = file.size >= sizeof record && hal_file_read(file, 0, record, sizeof record);
    hal_file_close(&file);
    for (int index = 0; read && index < MAGIC_BYTES; index++) {
        read = record[index] == MAGIC[index];
    }
    if (read) {
        copy_name(name, (const char *)record + MAGIC_BYTES, THEME_PACKAGE_NAME_BYTES - 1);
    }
    return read;
}

RIPPL_STARTUP_CODE static void load_start(int builtin)
{
    ThemeMemory *block = memory();
    char name[THEME_PACKAGE_NAME_BYTES];
    if (block == NULL) {
        return;
    }
    block->count = 0;
    block->builtin = (int8_t)builtin;
    apply(block, remembered(name) ? name : "");
}

RIPPL_STARTUP_CODE static void load_follow(int builtin)
{
    ThemeMemory *block = memory();
    if (block != NULL && !block->card && block->builtin != builtin) {
        block->builtin = (int8_t)builtin;
        apply(block, "");
    }
}

const ThemePackage *theme_store_package(void)
{
    const ThemeMemory *block = memory();
    return block != NULL && block->active ? &block->package : NULL;
}

bool theme_store_card(void)
{
    const ThemeMemory *block = memory();
    return block != NULL && block->card;
}

const char *theme_store_accent_name(int index)
{
    ThemeMemory *block = memory();
    if (block == NULL || !block->active || !block->file_ready || index < 0 ||
        index >= block->package.accent_count) {
        return "";
    }
    const uint8_t *name = file_bytes() + block->package.accents_at +
                          (uint32_t)index * THEME_PACKAGE_ACCENT_BYTES + THEME_ACCENT_NAME_FIELD;
    copy_name(block->accent_name, (const char *)name, THEME_ACCENT_NAME_BYTES - 1);
    return block->accent_name;
}

const ThemeIcons *theme_store_icons(void)
{
    const ThemeMemory *block = memory();
    return block != NULL && block->active ? &block->icons : NULL;
}

const Wallpaper *theme_store_wallpaper(bool portrait)
{
    ThemeMemory *block = memory();
    if (block == NULL || !block->active) {
        return NULL;
    }
    return theme_assets_wallpaper(&block->assets, &block->package, portrait);
}

static uint32_t engine_wipes;

static bool tend_engine(void)
{
    if (theme_engine() != NULL) {
        return false;
    }
    theme_engine_use(hal_system_load_theme_engine());
    engine_wipes = hal_system_theme_asset_wipes();
    return theme_engine() != NULL;
}

void theme_store_watch_assets(void)
{
    if (theme_engine() != NULL && engine_wipes != hal_system_theme_asset_wipes()) {
        theme_engine_use(NULL);
    }
    ThemeMemory *block = memory();
    if (block == NULL || !block->active) {
        return;
    }
    theme_assets_watch(&block->assets);
    if (block->file_ready && block->file_wipes != hal_system_theme_asset_wipes()) {
        block->file_ready = false;
        block->icons = (ThemeIcons){0};
    }
}

RIPPL_STARTUP_CODE static bool load_tend(void)
{
    ThemeMemory *block = memory();
    ThemeSource source;
    if (block == NULL || !block->active) {
        return false;
    }
    bool reread = !block->file_ready && open_worn(block, &source) && read_package(block, &source);
    if (!theme_assets_wanted(&block->assets) || !open_worn(block, &source)) {
        return reread;
    }
    bool changed = theme_assets_tend(&block->assets, &block->package, &source);
    close_source(&source);
    return changed || reread;
}

uint8_t theme_store_revision(void)
{
    const ThemeMemory *block = memory();
    return block != NULL ? block->revision : 0;
}

RIPPL_STARTUP_HELPER static void note_file(ThemeMemory *block, const HalDirEntry *entry)
{
    size_t length = entry->directory || entry->hidden ? 0 : stem_length(entry->name);
    if (length > 0 && block->count < THEME_STORE_FILES) {
        copy_name(block->names[block->count], entry->name, length);
        block->count++;
    }
}

RIPPL_STARTUP_CODE static int load_scan(void)
{
    ThemeMemory *block = memory();
    HalDir dir;
    HalDirEntry entry;
    if (block == NULL) {
        return 0;
    }
    block->count = 0;
    if (!hal_dir_open(&dir, THEME_STORE_FOLDER)) {
        return 0;
    }
    while (block->count < THEME_STORE_FILES && hal_dir_next(&dir, &entry)) {
        note_file(block, &entry);
    }
    hal_dir_close(&dir);
    return block->count;
}

const char *theme_store_name(int index)
{
    const ThemeMemory *block = memory();
    return block != NULL && index >= 0 && index < block->count ? block->names[index] : "";
}

int theme_store_current(void)
{
    const ThemeMemory *block = memory();
    for (int index = 0; block != NULL && block->card && index < block->count; index++) {
        if (text_same(block->names[index], block->package.name)) {
            return index;
        }
    }
    return THEME_STORE_NONE;
}

RIPPL_STARTUP_CODE static bool load_choose(int index)
{
    ThemeMemory *block = memory();
    if (block == NULL) {
        return false;
    }
    apply(block, theme_store_name(index));
    remember(block);
    return block->card;
}

RIPPL_STARTUP_CODE static void load_choose_builtin(int builtin)
{
    ThemeMemory *block = memory();
    if (block == NULL) {
        return;
    }
    block->builtin = (int8_t)builtin;
    apply(block, "");
    remember(block);
}

RIPPL_STARTUP_CODE static bool load_wear(const char *name)
{
    ThemeMemory *block = memory();
    if (block == NULL) {
        return false;
    }
    apply(block, name);
    return block->card;
}

const char **theme_store_options(void)
{
    ThemeMemory *block = memory();
    return block != NULL ? block->options : NULL;
}

RIPPL_STARTUP_DATA const ThemeLoader THEME_LOADER = {
    .start = load_start,
    .follow = load_follow,
    .tend = load_tend,
    .scan = load_scan,
    .choose = load_choose,
    .choose_builtin = load_choose_builtin,
    .wear = load_wear,
};

static const ThemeLoader *open_loader(void)
{
    const AppStartup *startup = app_startup_open();
    return startup != NULL ? startup->themes : NULL;
}

void theme_store_start(int builtin)
{
    const ThemeLoader *loader = open_loader();
    if (loader != NULL) {
        loader->start(builtin);
    }
    app_startup_close();
}

void theme_store_follow(int builtin)
{
    const ThemeMemory *block = memory();
    if (block == NULL || block->card || block->builtin == builtin) {
        return;
    }
    const ThemeLoader *loader = open_loader();
    if (loader != NULL) {
        loader->follow(builtin);
    }
    app_startup_close();
}

bool theme_store_tend_assets(void)
{
    theme_store_watch_assets();
    bool engine = tend_engine();
    const ThemeMemory *block = memory();
    if (block == NULL || !block->active ||
        (block->file_ready && !theme_assets_wanted(&block->assets))) {
        return engine;
    }
    const ThemeLoader *loader = open_loader();
    bool changed = loader != NULL && loader->tend();
    app_startup_close();
    return changed || engine;
}

int theme_store_scan(void)
{
    const ThemeLoader *loader = open_loader();
    int count = loader != NULL ? loader->scan() : 0;
    app_startup_close();
    return count;
}

bool theme_store_choose(int index)
{
    const ThemeLoader *loader = open_loader();
    bool card = loader != NULL && loader->choose(index);
    app_startup_close();
    return card;
}

void theme_store_choose_builtin(int builtin)
{
    const ThemeLoader *loader = open_loader();
    if (loader != NULL) {
        loader->choose_builtin(builtin);
    }
    app_startup_close();
}

bool theme_store_wear(const char *name)
{
    const ThemeLoader *loader = open_loader();
    bool card = loader != NULL && loader->wear(name);
    app_startup_close();
    return card;
}

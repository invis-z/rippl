#include "ui/theme_engine.h"

#include <stddef.h>

#include "core/resident.h"

static const ThemeEngine *engine;

RIPPL_ENGINE_ENTRY const ThemeEngine THEME_ENGINE = {
    .magic = THEME_ENGINE_MAGIC,
    .box = theme_engine_box,
    .wallpaper = theme_engine_wallpaper,
    .mask = theme_engine_mask,
};

void theme_engine_use(const ThemeEngine *loaded)
{
    engine = loaded != NULL && loaded->magic == THEME_ENGINE_MAGIC ? loaded : NULL;
}

const ThemeEngine *theme_engine(void)
{
    return engine;
}

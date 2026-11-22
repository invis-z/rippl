#include "app_state.h"

static int theme_key(const Settings *settings)
{
    return (int)settings->orientation | settings->options[OPTION_ACCENT] << 2 |
           settings->options[OPTION_HIGHLIGHT] << 5 | settings->options[OPTION_STATUS_BAR] << 7 |
           settings->options[OPTION_THEME] << 8 | theme_store_revision() << 12;
}

static void wear_spacing(ThemeMetrics *metrics, const uint8_t *spacing)
{
    for (int id = 0; id < THEME_SPACING_COUNT; id++) {
        if (spacing[id] != THEME_PACKAGE_SPACING_UNSET) {
            theme_space(metrics, (ThemeSpacingId)id, spacing[id] - 1);
        }
    }
}

static void wear_fonts(ThemeFonts *fonts)
{
    const Font **fields[] = {&fonts->title, &fonts->body, &fonts->small, &fonts->status};
    const Font *const builtin[THEME_FONT_COUNT] = {
#define THEME_FONT(id, name, height) DEFAULT_THEME.fonts.name,
#include "ui/theme_fonts.def"
#undef THEME_FONT
    };
    for (size_t field = 0; field < sizeof fields / sizeof fields[0]; field++) {
        for (int id = 0; id < THEME_FONT_COUNT; id++) {
            const Font *worn = theme_store_font((ThemeFontId)id);
            if (*fields[field] == builtin[id] && worn != NULL) {
                *fields[field] = worn;
                break;
            }
        }
    }
}

static void wear_theme_file(Theme *theme, Theme *stage)
{
    const ThemePackage *package = theme_store_package();
    if (package != NULL) {
        wear_fonts(&theme->fonts);
        wear_fonts(&stage->fonts);
        theme->icons = theme_store_icons();
        theme->wallpaper = theme_store_wallpaper(theme_is_portrait(theme));
        theme->layout = &package->layouts[theme_is_portrait(theme) ? THEME_WALLPAPER_PORTRAIT
                                                                   : THEME_WALLPAPER_LANDSCAPE];
        wear_spacing(&theme->metrics, package->spacing);
    }
}

int app_theme_key(void)
{
    return theme_key(&app.settings.settings);
}

void app_dress_theme(void)
{
    const Settings *settings = &app.settings.settings;
    theme_store_follow(settings->options[OPTION_THEME]);
    app.shown_theme = theme_key(settings);
    bool portrait = settings->orientation != ORIENTATION_LANDSCAPE;
    ThemeChoice choice = {.accent = settings->options[OPTION_ACCENT],
                          .flat = settings->options[OPTION_HIGHLIGHT] != 0};
    const ThemePackage *package = theme_store_package();
    app.theme = default_theme_for(portrait);
    app.stage_theme = app.theme;
    default_theme_dress(&app.theme, package, choice, &app.colors);
    default_theme_stage(&app.stage_theme, package, choice, &app.colors);
    if (settings->options[OPTION_STATUS_BAR] == STATUS_BAR_LARGE) {
        default_theme_enlarge_status(&app.theme);
        default_theme_enlarge_status(&app.stage_theme);
    }
    wear_theme_file(&app.theme, &app.stage_theme);
}

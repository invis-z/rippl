#include "app/app.h"

#include "app_state.h"

static Color strip_pixels[HAL_DISPLAY_WIDTH * STRIP_HEIGHT] __attribute__((aligned(4)));

uint32_t app_row_hashes[HAL_DISPLAY_LONG_SIDE];

static void draw_fade(Canvas *canvas, const Fade *fade, const char *text)
{
    const Theme *theme = app_current_theme();
    Rect screen = app_whole_screen();
    Color color =
        color_blend(theme->colors->background, theme->colors->text, fade_alpha(fade, app.now_ms));
    TextStyle style = {.font = theme->fonts.title, .color = color};
    label_draw(canvas, style,
               (Label){.area = label_centered_area(style.font, screen, text), .text = text});
}

static void draw_splash(Canvas *canvas)
{
    Rect screen = app_whole_screen();
    const RipplLogo *logo = &RIPPL_LOGO_SPLASH;
    Point origin = {screen.x + (screen.width - logo->side) / 2,
                    screen.y + (screen.height - logo->side) / 2};
    rippl_logo_draw(canvas, logo, origin, app_current_theme()->colors->text,
                    fade_alpha(&app.splash, app.now_ms));
}

static bool draw_running_fade(Canvas *canvas)
{
    if (app.saying_goodbye) {
        draw_fade(canvas, &app.goodbye, app.goodbye_text);
        return true;
    }
    if (!fade_finished(&app.splash, app.now_ms)) {
        draw_splash(canvas);
        return true;
    }
    return false;
}

static void draw_shifted(Screen *screen, Canvas *canvas, int x, Rect area)
{
    Canvas shifted = canvas_shifted(canvas, x, area);
    if (!rect_is_empty(shifted.clip)) {
        screen->draw(screen->context, &shifted);
    }
}

static const Screen *chrome_screen(const Screen *top)
{
    bool leaving = app_slide_running() && !slide_shows_arriving_title(&app.slide, app.now_ms);
    return leaving ? app.leaving : top;
}

static void draw_slide(Canvas *canvas, Screen *arriving)
{
    const Theme *theme = app_current_theme();
    Screen *leaving = app.leaving;
    bool whole = arriving->full_screen || leaving->full_screen;
    Rect area = whole ? app_whole_screen() : theme_content_area(theme);
    int width = theme->metrics.screen_width;
    draw_shifted(leaving, canvas, slide_leaving_x(&app.slide, app.now_ms, width), area);
    draw_shifted(arriving, canvas, slide_arriving_x(&app.slide, app.now_ms, width), area);
    if (!whole) {
        header_draw(canvas, theme, chrome_screen(arriving)->title);
    }
}

static void draw_frame(void *context, Canvas *canvas)
{
    AppState *state = context;
    if (draw_running_fade(canvas)) {
        return;
    }
    Screen *screen = screen_stack_top(&state->stack);
    if (app_slide_running()) {
        draw_slide(canvas, screen);
    } else {
        screen->draw(screen->context, canvas);
    }
    if (!screen->full_screen) {
        status_bar_draw(canvas, app_current_theme(), state->status_bar);
    }
    toast_draw(&state->toast, canvas, app_current_theme());
    if (state->options.show_frame_time) {
        FrameBreakdown breakdown = {
            .total_us = frame_stats_average_us(&state->frame_stats),
            .update_us = frame_stats_average_us(&state->update_stats),
            .draw_us = frame_stats_average_us(&state->draw_stats),
            .send_us = frame_stats_average_us(&state->send_stats),
        };
        frame_overlay_draw(canvas, app_current_theme(), breakdown, hal_audio_stats());
    }
}

static void write_to_display(void *context, Rect area, const Color *pixels)
{
    (void)context;
    Rect changed;
    uint32_t audio_started_us = hal_clock_microseconds();
    hal_audio_service();
    hal_bluetooth_service();
    uint32_t hash_started_us = hal_clock_microseconds();
    draw_profile_add(DRAW_PART_AUDIO, hash_started_us - audio_started_us);
    bool any_changed = frame_diff_changed(&app.frame_diff, area, pixels, &changed);
    app.frame_changed |= any_changed;
    draw_profile_add(DRAW_PART_HASH, hal_clock_microseconds() - hash_started_us);
    if (!any_changed) {
        return;
    }
    const Color *first_row = pixels + (size_t)(changed.y - area.y) * (size_t)area.width;
    uint32_t started_us = hal_clock_microseconds();
    hal_display_write(changed.x, changed.y, changed.width, changed.height, first_row);
    app.send_us += hal_clock_microseconds() - started_us;
    hal_audio_service();
}

static StatusPlayback playback_of(PlayerState state)
{
    switch (state) {
    case PLAYER_PLAYING:
        return STATUS_PLAYBACK_PLAYING;
    case PLAYER_PAUSED:
        return STATUS_PLAYBACK_PAUSED;
    case PLAYER_IDLE:
        return STATUS_PLAYBACK_NONE;
    }
    return STATUS_PLAYBACK_NONE;
}

static StatusBluetooth bluetooth_of(const BluetoothLink *link)
{
    if (!link->saved.enabled || link->dozing) {
        return STATUS_BLUETOOTH_OFF;
    }
    return link->connected ? STATUS_BLUETOOTH_CONNECTED : STATUS_BLUETOOTH_ON;
}

static const char *status_clock_text(void)
{
    static char text[WALL_CLOCK_TIME_CAPACITY];
    WallCivil civil;
    if (app.settings.settings.options[OPTION_CLOCK] != CLOCK_ON ||
        !clock_store_now(&app.clock, app.now_ms, &civil)) {
        return NULL;
    }
    wall_clock_time_text(&civil, text, sizeof text);
    return text;
}

StatusBarState app_current_status_bar(void)
{
    HalBattery battery = hal_power_battery();
    return (StatusBarState){
        .playback = playback_of(player_status(&app.player).state),
        .bluetooth = bluetooth_of(&app.bluetooth),
        .battery_known = battery.known,
        .battery_percent = battery.percent,
        .charging = battery.charging,
        .hide_percent = app.settings.settings.options[OPTION_BATTERY] != 0,
        .clock = status_clock_text(),
    };
}

static void paint_backdrop(void *context, Canvas *canvas)
{
    (void)context;
    backdrop_paint(canvas, app_current_theme(), canvas->clip);
}

void app_render(FrameDrawer drawer, bool drawer_paints_everything, int strip_height)
{
    theme_store_watch_assets();
    const Wallpaper *wallpaper = app_current_theme()->wallpaper;
    bool papered = wallpaper != NULL && wallpaper->ready;
    hal_display_wait();
    strip_height = (strip_height + 1) / 2;
    StripRenderer renderer = {
        .strip_pixels = strip_pixels,
        .other_pixels = strip_pixels + HAL_DISPLAY_WIDTH * STRIP_HEIGHT / 2,
        .screen_width = app_current_theme()->metrics.screen_width,
        .screen_height = app_current_theme()->metrics.screen_height,
        .strip_height = strip_height,
        .background = app_current_theme()->colors->background,
        .backdrop = {.draw = papered ? paint_backdrop : NULL},
        .drawer_paints_everything = drawer_paints_everything,
    };
    FrameSink sink = {.write = write_to_display, .context = NULL};
    strip_renderer_render(&renderer, drawer, sink);
    hal_display_present();
}

void app_render_frame(void)
{
    uint32_t started_us = hal_clock_microseconds();
    app.send_us = 0;
    app.frame_changed = false;
    app.status_bar = app_current_status_bar();
    bool screen_paints =
        app_top_screen()->paints_every_pixel && !app_fade_running() && !app_slide_running();
    marquee_frame_start();
    app_render((FrameDrawer){.draw = draw_frame, .context = &app}, screen_paints, STRIP_HEIGHT);
    uint32_t render_us = hal_clock_microseconds() - started_us;
    frame_stats_record(&app.send_stats, app.send_us);
    frame_stats_record(&app.draw_stats, render_us - app.send_us);
    draw_profile_finish_frame();
    app.frame_stable = !app.frame_changed || marquee_frame_paced();
    app.idle_render_ms = hal_clock_milliseconds() + IDLE_RENDER_MS;
    uint32_t marquee_wait_ms = marquee_frame_wait_ms();
    if (marquee_wait_ms > 0 &&
        !deadline_reached(app.now_ms + marquee_wait_ms, app.idle_render_ms)) {
        app.idle_render_ms = app.now_ms + marquee_wait_ms;
    }
}

void app_draw_notice(void *context, Canvas *canvas)
{
    Screen *screen = context;
    screen->draw(screen->context, canvas);
}

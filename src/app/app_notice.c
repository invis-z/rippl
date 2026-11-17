#include "app/app.h"

#include "app_state.h"

static void show_progress_notice(const char *title, const char *message,
                                 void (*draw)(void *, Canvas *), uint32_t done, uint32_t total)
{
    MessageConfig config = {.title = title,
                            .message = message,
                            .theme = app_current_theme(),
                            .progress = (uint16_t)done,
                            .progress_total = (uint16_t)total};
    Screen *screen = message_screen_init(&app.notice, config);
    if (!app.display_on) {
        app_set_display(true);
    }
    app_render((FrameDrawer){.draw = draw, .context = screen}, false, NOTICE_STRIP_HEIGHT);
    app_light_after_first_frame();
}

static void show_notice(const char *title, const char *message, void (*draw)(void *, Canvas *))
{
    show_progress_notice(title, message, draw, 0, 0);
}

void app_show_progress(const char *title, const char *message, uint32_t done, uint32_t total)
{
    show_progress_notice(title, message, app_draw_notice, done, total);
}

void app_show_notice(const char *title, const char *message)
{
    show_notice(title, message, app_draw_notice);
}

static void draw_usb_status(Canvas *canvas)
{
    StatusBarState status = app_current_status_bar();
    status.playback = STATUS_PLAYBACK_USB;
    status_bar_draw(canvas, app_current_theme(), status);
}

static void draw_usb_notice(void *context, Canvas *canvas)
{
    app_draw_notice(context, canvas);
    draw_usb_status(canvas);
}

void app_show_usb_notice(const char *title, const char *message)
{
    show_notice(title, message, draw_usb_notice);
}

static void draw_dac(void *context, Canvas *canvas)
{
    dac_screen_draw(canvas, app_current_theme(), context);
    draw_usb_status(canvas);
}

void app_show_dac(const AppDacView *view)
{
    static AppDacView shown;
    shown = *view;
    if (!app.display_on) {
        app_set_display(true);
    }
    app_render((FrameDrawer){.draw = draw_dac, .context = &shown}, true, STRIP_HEIGHT);
    app_light_after_first_frame();
}

void app_prepare_notice(const char *title, const char *message)
{
    const Theme *theme = app_current_theme();
    (void)text_width(theme->fonts.title, title);
    (void)text_width(theme->fonts.body, message);
}

void app_show_splash(void)
{
    app.now_ms = hal_clock_milliseconds();
    fade_start(&app.splash, app.now_ms, APP_SPLASH_TIMING);
}

void app_wake(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    power_policy_wake(&app.power, now_ms);
    clock_store_wake(&app.clock, now_ms);
    if (app.charging) {
        app_light_charge_screen(now_ms);
    }
}

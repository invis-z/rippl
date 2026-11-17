#include "app/app.h"

#include "app_state.h"

static void power_off_now(void)
{
    app_set_display(false);
    app.powered_off = true;
    hal_power_off();
}

void app_light_charge_screen(uint32_t now_ms)
{
    app.charge_lit_until_ms = now_ms + CHARGE_LIT_MS;
    if (!app.display_on) {
        app_set_display(true);
    }
}

void app_start_charging(uint32_t now_ms)
{
    if (player_status(&app.player).state == PLAYER_PLAYING) {
        player_toggle_pause(&app.player);
    }
    player_release_output(&app.player);
    bluetooth_link_sleep(&app.bluetooth);
    app.charging = true;
    app.charge_battery = hal_power_battery();
    app_light_charge_screen(now_ms);
}

static void stop_charging(uint32_t now_ms)
{
    app.charging = false;
    bluetooth_link_start(&app.bluetooth, &app.toast, now_ms);
    power_policy_wake(&app.power, now_ms);
    if (!app.display_on) {
        app_set_display(true);
    }
    app_show_splash();
}

void app_finish_shut_down(void)
{
    app.saying_goodbye = false;
    if (hal_power_battery().usb_power) {
        app_start_charging(app.now_ms);
        return;
    }
    power_off_now();
}

void app_shut_down(const char *text, bool shown)
{
    app.goodbye_text = text;
    app_save_session();
    if (shown && !app.display_on) {
        power_policy_wake(&app.power, app.now_ms);
        app_set_display(true);
    }
    if (!app.display_on) {
        app_finish_shut_down();
        return;
    }
    fade_start(&app.goodbye, app.now_ms, APP_GOODBYE_TIMING);
    app.saying_goodbye = true;
}

static bool battery_empty(uint32_t now_ms)
{
    if (!deadline_reached(now_ms, app.battery_check_ms)) {
        return false;
    }
    app.battery_check_ms = now_ms + BATTERY_CHECK_MS;
    HalBattery battery = hal_power_battery();
    app.usb_power = battery.usb_power;
    LowBatteryReading reading = {
        .known = battery.known, .millivolts = battery.millivolts, .usb_power = battery.usb_power};
    return low_battery_update(&app.low_battery, reading, now_ms);
}

static void keep_awake_while_busy(uint32_t now_ms)
{
    if (app.usb_power || player_status(&app.player).state == PLAYER_PLAYING) {
        power_policy_stay_awake(&app.power, now_ms);
    }
}

void app_update_power(uint32_t now_ms)
{
    if (!app.saying_goodbye && battery_empty(now_ms)) {
        app_shut_down(i18n_text(STR_BOOT_BATTERY_LOW), false);
        return;
    }
    keep_awake_while_busy(now_ms);
    if (power_policy_update(&app.power, now_ms)) {
        app_shut_down(APP_GOODBYE_TEXT, false);
        return;
    }
    if (app.power.display_on == app.display_on) {
        return;
    }
    app_set_display(app.power.display_on);
    if (!app.display_on) {
        app_save_settings();
    } else {
        app_show_now_playing(now_ms);
    }
}

static void draw_charge(void *context, Canvas *canvas)
{
    (void)context;
    charge_screen_draw(canvas, app_current_theme(), app.charge_battery);
}

static void follow_charge_keys(uint32_t now_ms)
{
    KeyEvent event;
    while (app.charging && key_tracker_next_event(&app.keys, &event)) {
        if (event.key == KEY_POWER && event.gesture == KEY_GESTURE_LONG_PRESS) {
            stop_charging(now_ms);
        } else {
            app_light_charge_screen(now_ms);
        }
    }
}

void app_charge_step(uint32_t now_ms)
{
    follow_charge_keys(now_ms);
    player_flush_save(&app.player);
    if (!app.charging) {
        return;
    }
    if (deadline_reached(now_ms, app.battery_check_ms)) {
        app.battery_check_ms = now_ms + BATTERY_CHECK_MS;
        app.charge_battery = hal_power_battery();
        if (!app.charge_battery.usb_power) {
            app.charging = false;
            power_off_now();
            return;
        }
    }
    if (app.display_on && deadline_reached(now_ms, app.charge_lit_until_ms)) {
        app_set_display(false);
    }
    if (app.display_on) {
        app_render((FrameDrawer){.draw = draw_charge, .context = NULL}, true, STRIP_HEIGHT);
        app_light_after_first_frame();
    } else {
        app_use_wanted_clock();
    }
}

bool app_charging(void)
{
    return app.charging;
}

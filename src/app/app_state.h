#pragma once

#include <stddef.h>

#include "app_internal.h"
#include "app_screens.h"
#include "bluetooth_link.h"
#include "card_files.h"
#include "clock_store.h"
#include "core/deadline.h"
#include "core/i18n.h"
#include "core/key_tracker.h"
#include "core/low_battery.h"
#include "core/power_policy.h"
#include "core/string_builder.h"
#include "core/volume_path.h"
#include "cover_cache.h"
#include "dac_screen.h"
#include "default_theme.h"
#include "draw_profile.h"
#include "equalizer_fits.h"
#include "equalizer_store.h"
#include "error_log.h"
#include "favorites_store.h"
#include "frame_overlay.h"
#include "gfx/frame_diff.h"
#include "gfx/strip_renderer.h"
#include "hal/audio.h"
#include "hal/bluetooth.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "hal/input.h"
#include "hal/language.h"
#include "hal/power.h"
#include "hal/system.h"
#include "language_files.h"
#include "library_rescan.h"
#include "library_store.h"
#include "music_check.h"
#include "player.h"
#include "screens/charge_screen.h"
#include "settings/rocker.h"
#include "settings_store.h"
#include "ui/choice_screen.h"
#include "ui/fade.h"
#include "ui/header.h"
#include "ui/label.h"
#include "ui/marquee.h"
#include "ui/message_screen.h"
#include "ui/rippl_logo.h"
#include "ui/screen_stack.h"
#include "ui/slide.h"
#include "ui/status_bar.h"
#include "ui/toast.h"

enum {
    STRIP_HEIGHT = 34,
    NOTICE_STRIP_HEIGHT = 6,
    RESCAN_PROGRESS_MS = 250,
    BATTERY_CHECK_MS = 250,
    CHARGE_LIT_MS = 5000,
    IDLE_RENDER_MS = 100,
    NUMBER_CAPACITY = 12,
    PROGRESS_TEXT_CAPACITY = 64,
    MESSAGE_TEXT_CAPACITY = 64
};

typedef struct AppState {
    KeyTracker keys;
    bool rocker_swapped[KEY_COUNT];
    uint32_t dac_volume_until_ms;
    Theme theme;
    Theme stage_theme;
    int shown_theme;
    FrameStats frame_stats;
    FrameStats update_stats;
    FrameStats draw_stats;
    FrameStats send_stats;
    uint32_t send_us;
    ScreenStack stack;
    LibraryStore library;
    Player player;
    SettingsStore settings;
    EqualizerStore equalizer;
    FavoritesStore favorites;
    SpokenStore spoken;
    ClockStore clock;
    bool usb_written;
    CoverCache covers;
    PowerPolicy power;
    LowBattery low_battery;
    uint32_t battery_check_ms;
    bool usb_power;
    bool rescan_requested;
    bool music_check_requested;
    bool clock_question;
    bool open_date_time;
    bool light_after_frame;
    bool frame_changed;
    bool frame_stable;
    bool key_activity;
    bool idle;
    uint32_t idle_render_ms;
    const char *goodbye_text;
    Toast toast;
    BluetoothLink bluetooth;
    uint32_t shown_refusals;
    StatusBarState status_bar;
    AppScreens screens;
    MessageScreen notice;
    ChoiceScreen prompt;
    ChoiceScreen clock_prompt;
    int prompt_default;
    int prompt_result;
    FrameDiff frame_diff;
    Fade splash;
    Fade goodbye;
    Slide slide;
    Screen *leaving;
    bool saying_goodbye;
    uint32_t now_ms;
    AppOptions options;
    bool display_on;
    bool powered_off;
    bool start_refused;
    uint32_t refused_ms;
    bool charging;
    uint32_t charge_lit_until_ms;
    HalBattery charge_battery;
} AppState;

extern AppState app;
extern const KeyConfig APP_KEY_CONFIG;

const Theme *app_current_theme(void);
void app_light_after_first_frame(void);
void app_read_input(void);
void app_render(FrameDrawer drawer, bool drawer_paints_everything, int strip_height);
void app_set_display(bool on);
void app_use_screen_key_behaviors(void);
Screen *app_top_screen(void);
Rect app_whole_screen(void);
void app_use_wanted_clock(void);
void app_save_settings(void);
void app_save_session(void);
bool app_fade_running(void);
bool app_slide_running(void);
void app_show_now_playing(uint32_t now_ms);
void app_push_screen(Screen *screen, uint32_t now_ms);
extern const FadeTiming APP_SPLASH_TIMING;
extern const FadeTiming APP_GOODBYE_TIMING;
extern const char APP_GOODBYE_TEXT[];
void app_light_charge_screen(uint32_t now_ms);
void app_start_charging(uint32_t now_ms);
void app_finish_shut_down(void);
void app_shut_down(const char *text, bool shown);
void app_update_power(uint32_t now_ms);
void app_charge_step(uint32_t now_ms);
StatusBarState app_current_status_bar(void);
void app_render_frame(void);
void app_draw_notice(void *context, Canvas *canvas);
extern uint32_t app_row_hashes[HAL_DISPLAY_LONG_SIDE];
void app_check_debug_file(void);
void app_report_card_files(CardFilesReport files, uint32_t now_ms);
void app_prepare_equalizer_fits(void);
void app_show_card_setup(void);
void app_explain_refusals(uint32_t now_ms);
void app_run_rescan(void);
void app_follow_clock_prompt(uint32_t now_ms);
void app_take_computer_clock(void);

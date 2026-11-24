#include "app/app.h"
#include "app/startup.h"

#include "app/startup.h"
#include "app_state.h"
#include "core/resident.h"

const KeyConfig APP_KEY_CONFIG = {
    .timing =
        {
            .long_press_ms = 600,
            .repeat_delay_ms = 400,
            .repeat_interval_ms = 120,
        },
    .behaviors =
        {
            [KEY_VOLUME_UP] = KEY_BEHAVIOR_REPEAT,
            [KEY_VOLUME_DOWN] = KEY_BEHAVIOR_REPEAT,
            [KEY_PLAY] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_MENU] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_BACK] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_POWER] = KEY_BEHAVIOR_TAP_OR_HOLD,
        },
    .long_press_ms =
        {
            [KEY_VOLUME_UP] = 1000,
            [KEY_VOLUME_DOWN] = 1000,
            [KEY_POWER] = 2000,
        },
};

AppState app;

const FadeTiming APP_SPLASH_TIMING = {.fade_ms = 300, .hold_ms = 200, .rising = true};
static const uint32_t SCREEN_ON_CLOCK_MHZ = 200;
static const uint32_t IDLE_CLOCK_MHZ = 96;
const FadeTiming APP_GOODBYE_TIMING = {.fade_ms = 600, .hold_ms = 400, .rising = false};
const char APP_GOODBYE_TEXT[] = "Goodbye";
static const char START_CODE_MISSING[] = "Start-up code missing: reinstall the update";
static const uint32_t START_REFUSAL_MS = 5000;

Screen *app_top_screen(void)
{
    return screen_stack_top(&app.stack);
}

const Theme *app_current_theme(void)
{
    return app.theme.metrics.screen_width > 0 ? &app.theme : &DEFAULT_THEME;
}

Rect app_whole_screen(void)
{
    const ThemeMetrics *metrics = &app_current_theme()->metrics;
    return (Rect){0, 0, metrics->screen_width, metrics->screen_height};
}

static HalDisplayRotation rotation_for(Orientation orientation)
{
    switch (orientation) {
    case ORIENTATION_PORTRAIT:
        return HAL_DISPLAY_PORTRAIT;
    case ORIENTATION_PORTRAIT_FLIPPED:
        return HAL_DISPLAY_PORTRAIT_FLIPPED;
    case ORIENTATION_LANDSCAPE:
        return HAL_DISPLAY_LANDSCAPE;
    }
    return HAL_DISPLAY_LANDSCAPE;
}

HalDisplayRotation app_display_rotation(void)
{
    return rotation_for(app.settings.settings.orientation);
}

static void apply_orientation(void)
{
    app_dress_theme();
    app.landscape_write = false;
    hal_display_set_rotation(app_display_rotation());
    frame_diff_init(&app.frame_diff, app_row_hashes, app.theme.metrics.screen_height);
}

static void follow_orientation(void)
{
    if (app_theme_key() != app.shown_theme) {
        apply_orientation();
        app.frame_stable = false;
    }
    if (theme_store_tend_assets()) {
        app.frame_stable = false;
    }
    app.power.menu_is_media = app.settings.settings.options[OPTION_MENU_KEY] == MENU_KEY_NEXT;
}

static Key oriented_key(HalKeyTransition transition)
{
    if (transition.pressed) {
        const Settings *settings = &app.settings.settings;
        app.rocker_swapped[transition.key] = rocker_swapped_in(settings->orientation);
    }
    return rocker_key(transition.key, app.rocker_swapped[transition.key]);
}

void app_read_input(void)
{
    HalKeyTransition transition;
    while (hal_input_next_transition(&transition)) {
        Key key = oriented_key(transition);
        if (transition.pressed) {
            key_tracker_key_down(&app.keys, key, transition.time_ms);
        } else {
            key_tracker_key_up(&app.keys, key, transition.time_ms);
        }
    }
}

void app_use_wanted_clock(void)
{
    hal_audio_allow_slow_clock(!app.display_on);
    if (!hal_audio_status().idle) {
        return;
    }
    uint32_t wanted = app.display_on ? SCREEN_ON_CLOCK_MHZ : IDLE_CLOCK_MHZ;
    if (hal_system_clock_mhz() != wanted) {
        hal_system_set_clock_mhz(wanted);
    }
}

void app_light_after_first_frame(void)
{
    if (app.light_after_frame) {
        app.light_after_frame = false;
        hal_display_set_power(true);
    }
}

void app_set_display(bool on)
{
    app.display_on = on;
    app.light_after_frame = on;
    app.frame_stable = false;
    if (on) {
        frame_diff_forget(&app.frame_diff);
    } else {
        hal_display_set_power(false);
    }
    app_use_wanted_clock();
    if (screen_stack_depth(&app.stack) > 0) {
        app_use_screen_key_behaviors();
    }
}

void app_save_settings(void)
{
    settings_store_capture(&app.settings, player_status(&app.player));
    settings_store_save(&app.settings);
    equalizer_store_save(&app.equalizer);
}

void app_save_session(void)
{
    spoken_store_flush(&app.spoken, player_status(&app.player), hal_clock_milliseconds());
    player_save(&app.player);
    app_save_settings();
}

bool app_fade_running(void)
{
    return app.saying_goodbye || !fade_finished(&app.splash, app.now_ms);
}

static void drain_key_events(void)
{
    KeyEvent event;
    while (key_tracker_next_event(&app.keys, &event)) {
    }
}

void app_use_screen_key_behaviors(void)
{
    bool hold = !app.display_on || app_top_screen()->hold_volume_keys;
    KeyBehavior volume = hold ? KEY_BEHAVIOR_TAP_HOLD_REPEAT : KEY_BEHAVIOR_REPEAT;
    key_tracker_set_behavior(&app.keys, KEY_VOLUME_UP, volume);
    key_tracker_set_behavior(&app.keys, KEY_VOLUME_DOWN, volume);
    bool at_once = app.display_on && app_top_screen()->select_on_press;
    key_tracker_set_behavior(&app.keys, KEY_PLAY,
                             at_once ? KEY_BEHAVIOR_PRESS : KEY_BEHAVIOR_TAP_OR_HOLD);
}

static bool opens_now_playing(KeyEvent event)
{
    return event.key == KEY_MENU && event.gesture == KEY_GESTURE_PRESS &&
           player_status(&app.player).has_track &&
           app_top_screen() != &app.screens.now_playing.screen;
}

static bool settings_open(void)
{
    for (int index = 0; index < app.stack.depth; index++) {
        if (app.stack.screens[index] == &app.screens.settings.menu.screen) {
            return true;
        }
    }
    return false;
}

static bool opens_settings(KeyEvent event)
{
    return event.key == KEY_MENU && event.gesture == KEY_GESTURE_LONG_PRESS &&
           app_top_screen() != &app.screens.now_playing.screen && !settings_open();
}

static ScreenAction now_playing_action(void)
{
    return app_top_screen() == &app.screens.up_next.screen
               ? screen_action_pop()
               : screen_action_push(&app.screens.now_playing.screen);
}

static void start_slide(Screen *leaving, int depth_before, uint32_t now_ms)
{
    if (app_top_screen() == leaving) {
        return;
    }
    bool deeper = screen_stack_depth(&app.stack) > depth_before;
    app.leaving = leaving;
    slide_start(&app.slide, now_ms, deeper ? SLIDE_FORWARD : SLIDE_BACK,
                settings_animation_ms(app.settings.settings.options, SLIDE_DURATION_MS));
}

bool app_slide_running(void)
{
    return app.slide.active && !slide_finished(&app.slide, app.now_ms);
}

static void dispatch_to_screen(KeyEvent event, uint32_t now_ms)
{
    Screen *screen = app_top_screen();
    int depth_before = screen_stack_depth(&app.stack);
    ScreenAction action = opens_now_playing(event) ? now_playing_action()
                          : opens_settings(event)
                              ? screen_action_push(&app.screens.settings.menu.screen)
                              : screen->handle_key(screen->context, event, now_ms);
    if (screen_stack_apply(&app.stack, action, now_ms)) {
        app_use_screen_key_behaviors();
        start_slide(screen, depth_before, hal_clock_milliseconds());
    }
}

static bool may_show_now_playing(void)
{
    PlayerStatus status = player_status(&app.player);
    Screen *top = app_top_screen();
    return status.has_track && status.state == PLAYER_PLAYING &&
           top != &app.screens.now_playing.screen && !top->stays_on_wake;
}

void app_show_now_playing(uint32_t now_ms)
{
    if (may_show_now_playing() &&
        screen_stack_apply(&app.stack, screen_action_push(&app.screens.now_playing.screen),
                           now_ms)) {
        app_use_screen_key_behaviors();
    }
}

static void run_background(KeyEvent event)
{
    NowPlayingControls controls =
        now_playing_controls_of(app.settings.settings.options, app.settings.settings.orientation);
    NowPlayingCommand command = now_playing_command_with(event, false, controls);
    switch (command) {
    case NOW_PLAYING_TOGGLE_PAUSE:
        player_toggle_pause(&app.player);
        break;
    case NOW_PLAYING_NEXT:
        player_skip(&app.player, 1);
        break;
    case NOW_PLAYING_PREVIOUS:
        player_skip(&app.player, -1);
        break;
    case NOW_PLAYING_VOLUME_UP:
        player_change_volume(&app.player, 1);
        break;
    case NOW_PLAYING_VOLUME_DOWN:
        player_change_volume(&app.player, -1);
        break;
    case NOW_PLAYING_SEEK_FORWARD:
    case NOW_PLAYING_SEEK_BACK:
        player_jump(&app.player, command == NOW_PLAYING_SEEK_FORWARD ? NOW_PLAYING_SCRUB_STEP_MS
                                                                     : -NOW_PLAYING_SCRUB_STEP_MS);
        break;
    default:
        break;
    }
}

static bool ignored_power_tap(KeyEvent event)
{
    return app.options.ignore_power_taps && event.key == KEY_POWER &&
           event.gesture == KEY_GESTURE_PRESS;
}

static void dispatch_key_events(uint32_t now_ms)
{
    player_sync(&app.player);
    KeyEvent event;
    while (!app.powered_off && key_tracker_next_event(&app.keys, &event)) {
        app.key_activity = true;
        if (ignored_power_tap(event)) {
            continue;
        }
        PowerKeyOutcome outcome = power_policy_key(&app.power, event, now_ms);
        if (outcome.shutdown) {
            app_shut_down(APP_GOODBYE_TEXT, true);
        } else if (outcome.background) {
            run_background(event);
        } else if (!outcome.consumed) {
            dispatch_to_screen(event, now_ms);
        }
    }
}

static void update_screen(uint32_t now_ms)
{
    Screen *screen = app_top_screen();
    if (screen->update != NULL) {
        screen->update(screen->context, now_ms);
    }
}

static void flush_save(void)
{
    if (!app.player.save_pending || app_slide_running() || app_fade_running()) {
        return;
    }
    hal_audio_service();
    player_flush_save(&app.player);
}

static bool frame_needed(uint32_t now_ms)
{
    bool busy = app.key_activity || app.options.show_frame_time || app.light_after_frame ||
                app_fade_running() || app_slide_running();
    return busy || !app.frame_stable || deadline_reached(now_ms, app.idle_render_ms);
}

RIPPL_STARTUP_CODE static void start_playback(void)
{
    cover_cache_close(&app.covers);
    library_store_open(&app.library);
    player_init(&app.player, &app.library.library);
    settings_store_restore_playback(&app.settings, &app.player, hal_clock_microseconds());
    player_restore(&app.player);
    favorites_store_load(&app.favorites, &app.library.library);
    spoken_store_load(&app.spoken, &app.library.library);
    player_set_resume(&app.player, spoken_store_resume(&app.spoken));
    cover_cache_open(&app.covers, &app.library.library, &app.library.segments);
}

RIPPL_STARTUP_CODE static void start_hardware(uint32_t now_ms)
{
    power_policy_init(&app.power, settings_screen_timeout_ms(&app.settings.settings), now_ms);
    power_policy_set_auto_off(&app.power, settings_auto_off_ms(&app.settings.settings), now_ms);
    settings_store_apply_hardware(&app.settings.settings);
    equalizer_store_apply(&app.equalizer);
    app_set_display(true);
}

static void open_start_screen(uint32_t now_ms)
{
    Screen *path[START_PATH_CAPACITY];
    int count = app_screens_start_path(&app.screens, app.settings.settings.start_screen, path);
    for (int index = 0; index < count; index++) {
        screen_stack_apply(&app.stack, screen_action_push(path[index]), now_ms);
    }
}

RIPPL_STARTUP_CODE static void start_screens(uint32_t now_ms)
{
    AppScreensContext context = {
        .options = &app.options,
        .theme = &app.theme,
        .stage_theme = &app.stage_theme,
        .store = &app.library,
        .player = &app.player,
        .spoken = &app.spoken,
        .settings = &app.settings,
        .favorites = &app.favorites,
        .power = &app.power,
        .toast = &app.toast,
        .covers = &app.covers,
        .rescan_requested = &app.rescan_requested,
        .bluetooth = &app.bluetooth,
        .equalizer = &app.equalizer,
        .clock = &app.clock,
    };
    Screen *root = app_screens_init(&app.screens, context);
    screen_stack_init(&app.stack, root, now_ms);
    open_start_screen(now_ms);
    app_use_screen_key_behaviors();
}

static bool attach_language(Language language)
{
    if ((int)language >= LANGUAGE_BUILTIN_COUNT) {
        return language_files_attach(language);
    }
    const uint8_t *pack;
    uint32_t size;
    i18n_detach();
    return hal_language_pack((uint32_t)language, &pack, &size) && i18n_attach(pack, size);
}

RIPPL_STARTUP_CODE static void apply_language(Language language)
{
    if (attach_language(language)) {
        return;
    }
    char path[LANGUAGE_FILE_PATH_CAPACITY];
    if ((int)language >= LANGUAGE_BUILTIN_COUNT &&
        language_files_path(language, path, sizeof path)) {
        error_log_add("can't load the language pack", path);
    }
    if (!attach_language(LANGUAGE_ENGLISH)) {
        i18n_detach();
    }
}

RIPPL_STARTUP_CODE static void start_up(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    apply_language(LANGUAGE_ENGLISH);
    key_tracker_init(&app.keys, &APP_KEY_CONFIG);
    frame_stats_reset(&app.frame_stats);
    frame_stats_reset(&app.update_stats);
    frame_stats_reset(&app.draw_stats);
    frame_stats_reset(&app.send_stats);
    draw_profile_use_clock(hal_clock_microseconds);
    app.options =
        (AppOptions){.show_frame_time = false, .ignore_power_taps = false, .log_fits = false};
    app.toast = (Toast){.visible = false};
    app.powered_off = false;
    app.charging = false;
    app.frame_stable = false;
    app.key_activity = false;
    app.idle = false;
    app.saying_goodbye = false;
    app.goodbye_text = APP_GOODBYE_TEXT;
    low_battery_init(&app.low_battery);
    app.battery_check_ms = now_ms;
    app.splash = (Fade){.active = false};
    app.now_ms = now_ms;
    CardFilesReport files = card_files_prepare(app_show_card_setup);
    settings_store_load(&app.settings);
    theme_store_start(app.settings.settings.options[OPTION_THEME]);
    hal_power_confirm_start(app.settings.settings.options[OPTION_POWER_ON] == POWER_ON_HOLD);
    clock_store_start(&app.clock, &app.settings.settings, now_ms);
    error_log_forget();
    error_log_use_clock(&app.clock);
    apply_language(app.settings.settings.language);
    apply_orientation();
    equalizer_store_load(&app.equalizer);
    start_playback();
    start_hardware(now_ms);
    bluetooth_link_start(&app.bluetooth, &app.toast, now_ms);
    app_check_debug_file();
    start_screens(now_ms);
    equalizer_screen_refresh(&app.screens.equalizer);
    app_report_card_files(files, now_ms);
    if (hal_power_take_usb_start()) {
        app_start_charging(now_ms);
    }
}

RIPPL_STARTUP_ENTRY const AppStartup APP_STARTUP = {
    .magic = APP_STARTUP_MAGIC, .start = start_up, .themes = &THEME_LOADER};

static void refuse_to_start(void)
{
    bool updated = hal_system_restart_pending();
    app_show_notice("Rippl", updated ? i18n_text(STR_RESCAN_RESTART_NEEDED) : START_CODE_MISSING);
    app.refused_ms = hal_clock_milliseconds();
}

static void wait_after_refusal(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    if (hal_power_battery().usb_power) {
        app.refused_ms = now_ms;
        return;
    }
    if (now_ms - app.refused_ms >= START_REFUSAL_MS) {
        app.start_refused = false;
        app.powered_off = true;
        hal_power_off();
    }
}

static int startup_depth;
static const AppStartup *startup_loaded;

const AppStartup *app_startup_open(void)
{
    if (startup_depth++ == 0) {
        const AppStartup *startup = hal_system_load_startup();
        startup_loaded = startup != NULL && startup->magic == APP_STARTUP_MAGIC ? startup : NULL;
    }
    return startup_loaded;
}

void app_startup_close(void)
{
    if (--startup_depth == 0) {
        startup_loaded = NULL;
        hal_system_release_startup();
    }
}

void app_start(void)
{
    const AppStartup *startup = app_startup_open();
    bool usable = startup != NULL;
    if (usable) {
        startup->start();
    }
    app_startup_close();
    app.start_refused = !usable;
    if (!usable) {
        refuse_to_start();
    }
}

void app_push_screen(Screen *screen, uint32_t now_ms)
{
    if (screen_stack_apply(&app.stack, screen_action_push(screen), now_ms)) {
        app_use_screen_key_behaviors();
    }
}

void app_step(void)
{
    if (app.powered_off) {
        return;
    }
    if (app.start_refused) {
        wait_after_refusal();
        return;
    }
    app.idle = false;
    app_take_computer_clock();
    if (app.music_check_requested && fade_finished(&app.splash, hal_clock_milliseconds())) {
        app.music_check_requested = false;
        app_check_debug_file();
        bool changed = hal_system_restart_pending();
        if (!changed) {
            app_show_notice(i18n_text(STR_RESCAN_MUSIC_TITLE), i18n_text(STR_RESCAN_CHECKING));
            changed = !music_check_unchanged() || !library_store_open(&app.library);
        }
        if (!changed) {
            app_prepare_equalizer_fits();
            equalizer_store_apply(&app.equalizer);
        }
        if (!changed) {
            music_check_note_firmware();
            app_wake();
        }
        app.rescan_requested |= changed;
    }
    if (app.rescan_requested) {
        app.rescan_requested = false;
        app_run_rescan();
        return;
    }
    uint32_t started_us = hal_clock_microseconds();
    uint32_t now_ms = hal_clock_milliseconds();
    app.now_ms = now_ms;
    hal_audio_service();
    hal_bluetooth_service();
    app_read_input();
    key_tracker_update(&app.keys, now_ms);
    if (app.charging) {
        app_charge_step(now_ms);
        return;
    }
    if (app_fade_running()) {
        drain_key_events();
    } else {
        dispatch_key_events(now_ms);
    }
    app_follow_clock_prompt(now_ms);
    follow_orientation();
    if (app.saying_goodbye && fade_finished(&app.goodbye, now_ms)) {
        app_finish_shut_down();
    }
    if (!app.powered_off) {
        clock_store_step(&app.clock, now_ms);
        player_update(&app.player, now_ms);
        bluetooth_link_step(&app.bluetooth, &app.player, now_ms);
        PlayerStatus status = player_status(&app.player);
        spoken_store_follow(&app.spoken, status, now_ms);
        app_explain_refusals(now_ms);
        app_update_power(now_ms);
    }
    if (app.powered_off) {
        return;
    }
    if (!app.display_on) {
        app_use_wanted_clock();
    }
    toast_update(&app.toast, now_ms);
    update_screen(now_ms);
    frame_stats_record(&app.update_stats, hal_clock_microseconds() - started_us);
    if (app.display_on && frame_needed(now_ms)) {
        app.now_ms = hal_clock_milliseconds();
        app_render_frame();
        app_light_after_first_frame();
    } else {
        app.idle = app.display_on;
    }
    app.key_activity = false;
    flush_save();
    frame_stats_record(&app.frame_stats, hal_clock_microseconds() - started_us);
}

bool app_idle(void)
{
    return app.idle;
}

const FrameStats *app_frame_stats(void)
{
    return &app.frame_stats;
}

const char *app_screen_title(void)
{
    return app_top_screen()->title;
}

int app_screen_depth(void)
{
    return screen_stack_depth(&app.stack);
}

void app_stop(void)
{
    if (!app.powered_off) {
        app_save_session();
    }
}

PlayerStatus app_player_status(void)
{
    return player_status(&app.player);
}

void app_now_playing_layout(ThemeLayout *out)
{
    now_playing_builtin_layout(&app.screens.now_playing, out);
}

const char *app_now_playing_lyric(void)
{
    return now_playing_lyric(&app.screens.now_playing);
}

const char *app_toast_text(void)
{
    return app.toast.visible ? app.toast.text : NULL;
}

const char *app_diagnostics_value(int index)
{
    const MenuItem *item = &app.screens.diagnostics_items[index];
    return item->value != NULL ? item->value(item->context) : NULL;
}

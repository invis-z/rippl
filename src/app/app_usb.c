#include "app/app.h"

#include "app_state.h"

static const KeyConfig DAC_KEY_CONFIG = {
    .timing =
        {
            .long_press_ms = 600,
            .repeat_delay_ms = 400,
            .repeat_interval_ms = 120,
        },
    .behaviors =
        {
            [KEY_VOLUME_UP] = KEY_BEHAVIOR_TAP_HOLD_REPEAT,
            [KEY_VOLUME_DOWN] = KEY_BEHAVIOR_TAP_HOLD_REPEAT,
            [KEY_PLAY] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_MENU] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_BACK] = KEY_BEHAVIOR_TAP_OR_HOLD,
            [KEY_POWER] = KEY_BEHAVIOR_TAP_OR_HOLD,
        },
    .long_press_ms =
        {
            [KEY_VOLUME_UP] = 1000,
            [KEY_VOLUME_DOWN] = 1000,
            [KEY_BACK] = 1000,
            [KEY_POWER] = 2000,
        },
};

void app_release_audio(void)
{
    spoken_store_flush(&app.spoken, player_status(&app.player), hal_clock_milliseconds());
    player_release_output(&app.player);
}

void app_remember_dac_volume(uint8_t level)
{
    app.settings.settings.dac_volume = level;
    settings_store_save(&app.settings);
}

void app_dac_keys_start(void)
{
    key_tracker_init(&app.keys, &DAC_KEY_CONFIG);
    app.dac_volume_until_ms = hal_clock_milliseconds();
}

static AppDacKey dac_key_of(NowPlayingCommand command, bool menu_skips)
{
    switch (command) {
    case NOW_PLAYING_EQUALIZER:
        return APP_DAC_KEY_STATS;
    case NOW_PLAYING_UP_NEXT:
        return menu_skips ? APP_DAC_KEY_STATS : APP_DAC_KEY_NONE;
    case NOW_PLAYING_VOLUME_UP:
        return APP_DAC_KEY_VOLUME_UP;
    case NOW_PLAYING_VOLUME_DOWN:
        return APP_DAC_KEY_VOLUME_DOWN;
    case NOW_PLAYING_TOGGLE_PAUSE:
        return APP_DAC_KEY_PLAY_PAUSE;
    case NOW_PLAYING_NEXT:
        return APP_DAC_KEY_NEXT;
    case NOW_PLAYING_PREVIOUS:
        return APP_DAC_KEY_PREVIOUS;
    case NOW_PLAYING_HOME:
        return APP_DAC_KEY_LEAVE;
    default:
        return APP_DAC_KEY_NONE;
    }
}

static AppDacKey dac_key_for(KeyEvent event, uint32_t now_ms)
{
    NowPlayingControls controls =
        now_playing_controls_of(app.settings.settings.options, app.settings.settings.orientation);
    controls.volume_shown = !deadline_reached(now_ms, app.dac_volume_until_ms);
    AppDacKey key =
        dac_key_of(now_playing_command_with(event, false, controls), controls.menu_skips);
    if (key == APP_DAC_KEY_VOLUME_UP || key == APP_DAC_KEY_VOLUME_DOWN) {
        app.dac_volume_until_ms = now_ms + NOW_PLAYING_VOLUME_HOLD_MS;
    }
    return key;
}

AppDacKey app_dac_next_key(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    app_read_input();
    key_tracker_update(&app.keys, now_ms);
    KeyEvent event;
    while (key_tracker_next_event(&app.keys, &event)) {
        AppDacKey key = dac_key_for(event, now_ms);
        if (key != APP_DAC_KEY_NONE) {
            return key;
        }
    }
    return APP_DAC_KEY_NONE;
}

static int prompt_current(const void *context)
{
    const AppState *state = context;
    return state->prompt_default;
}

static void prompt_choose(void *context, int index)
{
    AppState *state = context;
    state->prompt_result = index;
}

void app_choice_show(const char *title, const StringId *options, int count, int current)
{
    app.prompt_default = current;
    app.prompt_result = -1;
    ChoiceConfig config = {.title = title,
                           .options = NULL,
                           .option_ids = options,
                           .option_count = count,
                           .current = prompt_current,
                           .choose = prompt_choose,
                           .context = &app,
                           .theme = app_current_theme()};
    Screen *screen = choice_screen_init(&app.prompt, config);
    app.now_ms = hal_clock_milliseconds();
    screen->enter(screen->context, app.now_ms);
    key_tracker_init(&app.keys, &APP_KEY_CONFIG);
    key_tracker_set_behavior(&app.keys, KEY_PLAY, KEY_BEHAVIOR_PRESS);
    if (!app.display_on) {
        app_set_display(true);
    }
    app.frame_stable = false;
}

static void draw_prompt(void *context, Canvas *canvas)
{
    Screen *screen = context;
    screen->draw(screen->context, canvas);
}

static void follow_prompt_keys(uint32_t now_ms)
{
    KeyEvent event;
    Screen *screen = &app.prompt.screen;
    while (app.prompt_result < 0 && key_tracker_next_event(&app.keys, &event)) {
        ScreenAction action = screen->handle_key(screen->context, event, now_ms);
        bool dismissed =
            action.type == SCREEN_ACTION_POP || action.type == SCREEN_ACTION_POP_TO_ROOT;
        if (dismissed && app.prompt_result < 0) {
            app.prompt_result = app.prompt_default;
        }
    }
}

int app_choice_step(void)
{
    uint32_t now_ms = hal_clock_milliseconds();
    app.now_ms = now_ms;
    hal_audio_service();
    hal_bluetooth_service();
    app_read_input();
    key_tracker_update(&app.keys, now_ms);
    follow_prompt_keys(now_ms);
    Screen *screen = &app.prompt.screen;
    screen->update(screen->context, now_ms);
    app_render((FrameDrawer){.draw = draw_prompt, .context = screen}, false, STRIP_HEIGHT);
    app_light_after_first_frame();
    return app.prompt_result;
}

void app_choice_close(void)
{
    app.frame_stable = false;
    app_dac_keys_stop();
}

void app_dac_keys_stop(void)
{
    key_tracker_init(&app.keys, &APP_KEY_CONFIG);
    if (screen_stack_depth(&app.stack) > 0) {
        app_use_screen_key_behaviors();
    }
}

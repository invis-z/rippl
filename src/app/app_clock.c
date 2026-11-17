#include "app/app.h"

#include "app_state.h"

static const StringId CLOCK_PROMPT_OPTIONS[] = {STR_SETTINGS_SET_NOW, STR_SETTINGS_LATER};

static int clock_prompt_current(const void *context)
{
    (void)context;
    return 0;
}

static void clock_prompt_choose(void *context, int index)
{
    AppState *state = context;
    state->open_date_time = index == 0;
}

static void ask_for_the_clock(uint32_t now_ms)
{
    app.clock_question = false;
    if (app.clock.clock.known) {
        return;
    }
    ChoiceConfig config = {.title = i18n_text(STR_SETTINGS_CLOCK_PROMPT),
                           .option_ids = CLOCK_PROMPT_OPTIONS,
                           .option_count = 2,
                           .current = clock_prompt_current,
                           .choose = clock_prompt_choose,
                           .context = &app,
                           .theme = &app.theme};
    app_push_screen(choice_screen_init(&app.clock_prompt, config), now_ms);
}

void app_follow_clock_prompt(uint32_t now_ms)
{
    if (app.clock_question && !app.music_check_requested && !app_fade_running()) {
        ask_for_the_clock(now_ms);
    }
    if (app.open_date_time) {
        app.open_date_time = false;
        app_push_screen(&app.screens.date_time.screen, now_ms);
    }
}

void app_take_computer_clock(void)
{
    if (!app.usb_written) {
        return;
    }
    app.usb_written = false;
    error_log_forget();
    equalizer_screen_refresh(&app.screens.equalizer);
    uint32_t now_ms = hal_clock_milliseconds();
    if (clock_store_after_usb(&app.clock, &app.settings.settings, now_ms)) {
        settings_store_save(&app.settings);
        toast_show(&app.toast, i18n_text(STR_SETTINGS_CLOCK_SYNCED), now_ms);
    }
}

void app_forget_clock(void)
{
    app.clock.started = false;
}

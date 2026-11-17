#include "app_screens.h"

#include <stdbool.h>
#include <stddef.h>

#include "core/i18n.h"
#include "core/string_builder.h"
#include "diagnostics_rows.h"
#include "diagnostics_values.h"
#include "hal/audio.h"
#include "hal/power.h"
#include "hal/storage.h"
#include "hal/system.h"

enum {
    SD_READS_CAPACITY = 24
};

static ScreenAction toggle_performance(void *context)
{
    AppOptions *options = context;
    options->show_frame_time = !options->show_frame_time;
    return screen_action_none();
}

static const char *performance_value(const void *context)
{
    const AppOptions *options = context;
    return i18n_text(options->show_frame_time ? STR_ON : STR_OFF);
}

static ScreenAction toggle_fit_log(void *context)
{
    AppOptions *options = context;
    options->log_fits = !options->log_fits;
    return screen_action_none();
}

static const char *fit_log_value(const void *context)
{
    const AppOptions *options = context;
    return i18n_text(options->log_fits ? STR_ON : STR_OFF);
}

static ScreenAction toggle_power_taps(void *context)
{
    AppOptions *options = context;
    options->ignore_power_taps = !options->ignore_power_taps;
    return screen_action_none();
}

static const char *power_taps_value(const void *context)
{
    const AppOptions *options = context;
    return i18n_text(options->ignore_power_taps ? STR_DIAG_POWER_TAPS_IGNORED
                                                : STR_DIAG_POWER_TAPS_NORMAL);
}

static char clock_text[DIAGNOSTICS_VALUE_CAPACITY];

static ScreenAction cycle_clock(void *context)
{
    ClockOption *clock = context;
    clock->failed = !hal_system_set_clock_mhz(diagnostics_next_clock_mhz(hal_system_clock_mhz()));
    return screen_action_none();
}

static const char *clock_value(const void *context)
{
    const ClockOption *clock = context;
    diagnostics_clock_text(hal_system_clock_mhz(), clock->failed, clock_text, sizeof clock_text);
    return clock_text;
}

static char core1_text[DIAGNOSTICS_VALUE_CAPACITY];

static ScreenAction run_core1_test(void *context)
{
    Core1Option *core1 = context;
    core1->test = hal_system_core1_test();
    core1->run = true;
    return screen_action_none();
}

static const char *core1_value(const void *context)
{
    const Core1Option *core1 = context;
    diagnostics_core1_text(core1->run, core1->test, core1_text, sizeof core1_text);
    return core1_text;
}

static char core1_accel_text[DIAGNOSTICS_VALUE_CAPACITY];

static ScreenAction run_core1_accel_test(void *context)
{
    Core1AccelOption *accel = context;
    accel->result = hal_system_core1_accel_test();
    accel->run = true;
    return screen_action_none();
}

static const char *core1_accel_value(const void *context)
{
    const Core1AccelOption *accel = context;
    diagnostics_core1_accel_text(accel->run, accel->result, core1_accel_text,
                                 sizeof core1_accel_text);
    return core1_accel_text;
}

static ScreenAction cycle_audio_output(void *context)
{
    (void)context;
    hal_audio_set_word_output(!hal_audio_word_output());
    return screen_action_none();
}

static const char *audio_output_value(const void *context)
{
    (void)context;
    return hal_audio_word_output() ? "Core 1" : "Core 0";
}

static const char *sd_clock_value(const void *context)
{
    (void)context;
    static const char *const SPEEDS[] = {"24 MHz", "50 MHz", "Refused", "Fell back"};
    return SPEEDS[hal_storage_card_speed() % (sizeof SPEEDS / sizeof SPEEDS[0])];
}

static const char *sd_reads_value(const void *context)
{
    (void)context;
    uint32_t failures = hal_storage_card_dma_failures();
    if (failures == 0) {
        return "DMA";
    }
    static char text[SD_READS_CAPACITY];
    StringBuilder builder = string_builder_make(text, sizeof text);
    string_builder_append(&builder, "DMA, ");
    string_builder_append_unsigned(&builder, failures);
    string_builder_append(&builder, failures == 1 ? " retry" : " retries");
    return text;
}

static char boot_hold_text[DIAGNOSTICS_VALUE_CAPACITY];

static const char *boot_hold_value(const void *context)
{
    (void)context;
    diagnostics_boot_hold_text(hal_power_boot_hold(), boot_hold_text, sizeof boot_hold_text);
    return boot_hold_text;
}

static MenuItem value_item(StringId label, const char *(*value)(const void *context))
{
    return (MenuItem){.label = i18n_text(label), .select = NULL, .context = NULL, .value = value};
}

static MenuItem action_item(StringId label, ScreenAction (*select)(void *context), void *context,
                            const char *(*value)(const void *context))
{
    return (MenuItem){
        .label = i18n_text(label), .select = select, .context = context, .value = value};
}

typedef struct GroupRows {
    StringId title;
    uint8_t count;
} GroupRows;

static const GroupRows GROUPS[DIAGNOSTICS_GROUP_COUNT] = {
    {STR_DIAG_GROUP_AUDIO, 5}, {STR_DIAG_GROUP_DISPLAY, 1}, {STR_DIAG_GROUP_STORAGE, 2},
    {STR_DIAG_GROUP_USB, 2},   {STR_DIAG_GROUP_POWER, 5},   {STR_DIAG_GROUP_SYSTEM, 7},
    {STR_DIAG_GROUP_BOOT, 6}};

static ScreenAction open_group(void *context)
{
    DiagnosticsGroupEntry *entry = context;
    AppScreens *screens = entry->screens;
    uint32_t first = 0;
    for (uint32_t group = 0; group < entry->group; group++) {
        first += GROUPS[group].count;
    }
    MenuConfig config = {.title = i18n_text(GROUPS[entry->group].title),
                         .items = screens->diagnostics_items + first,
                         .item_count = GROUPS[entry->group].count,
                         .theme = screens->diagnostics_theme};
    return screen_action_push(menu_screen_init(&screens->diagnostics_group, config));
}

static int build_rows(AppScreens *screens, AppOptions *options, MenuItem *items)
{
    int count = 0;
    items[count++] =
        action_item(STR_DIAG_AUDIO_OUTPUT, cycle_audio_output, NULL, audio_output_value);
    items[count++] = value_item(STR_DIAG_JACK_SINGLE_ENDED, diagnostics_row_jack_single_ended);
    items[count++] = value_item(STR_DIAG_JACK_BALANCED, diagnostics_row_jack_balanced);
    items[count++] = (MenuItem){.label = i18n_text(STR_BT_TITLE),
                                .select = bluetooth_screen_open_details,
                                .context = &screens->bluetooth,
                                .value = NULL,
                                .opens = true};
    items[count++] = action_item(STR_DIAG_FIT_LOG, toggle_fit_log, options, fit_log_value);
    items[count++] = value_item(STR_DIAG_PANEL, diagnostics_row_panel);
    items[count++] = value_item(STR_DIAG_SD_CLOCK, sd_clock_value);
    items[count++] = value_item(STR_DIAG_SD_READS, sd_reads_value);
    items[count++] = value_item(STR_DIAG_LAST_USB, diagnostics_row_usb_last);
    items[count++] = value_item(STR_DIAG_USB_ERRORS, diagnostics_row_usb_errors);
    items[count++] = value_item(STR_DIAG_BATTERY, diagnostics_row_battery);
    items[count++] = value_item(STR_DIAG_LAST_BATTERY, diagnostics_row_last_battery);
    items[count++] = value_item(STR_DIAG_POWER, diagnostics_row_power);
    items[count++] = action_item(STR_DIAG_POWER_TAPS, toggle_power_taps, options, power_taps_value);
    items[count++] =
        action_item(STR_DIAG_SAVINGS, diagnostics_row_savings_next, NULL, diagnostics_row_savings);
    items[count++] = value_item(STR_DIAG_FIRMWARE, diagnostics_row_firmware);
    items[count++] = action_item(STR_DIAG_CPU_CLOCK, cycle_clock, &screens->clock, clock_value);
    items[count++] = action_item(STR_DIAG_CORE1, run_core1_test, &screens->core1, core1_value);
    items[count++] = action_item(STR_DIAG_LPC_ACCEL, run_core1_accel_test, &screens->core1_accel,
                                 core1_accel_value);
    items[count++] =
        action_item(STR_DIAG_CLOCK, diagnostics_row_clock_read, NULL, diagnostics_row_clock);
    items[count++] = app_screens_open_item(i18n_text(STR_DIAG_KEY_TEST),
                                           test_pattern_screen_init(&screens->test_pattern));
    items[count++] =
        action_item(STR_DIAG_PERFORMANCE, toggle_performance, options, performance_value);
    items[count++] = value_item(STR_DIAG_BOOT_HOLD, boot_hold_value);
    items[count++] = value_item(STR_DIAG_LAST_START, diagnostics_row_start);
    items[count++] = value_item(STR_DIAG_LAST_STEP, diagnostics_row_last_step);
    items[count++] = value_item(STR_DIAG_LAST_STACK, diagnostics_row_last_stack);
    items[count++] = value_item(STR_DIAG_STACK, diagnostics_row_stack);
    items[count++] = value_item(STR_DIAG_LAST_UPDATE, diagnostics_row_last_update);
    return count;
}

void diagnostics_menu_build(AppScreens *screens, AppOptions *options, const Theme *theme)
{
    screens->clock = (ClockOption){.failed = false};
    screens->core1 = (Core1Option){.run = false};
    screens->core1_accel = (Core1AccelOption){.run = false};
    screens->diagnostics_theme = theme;
    build_rows(screens, options, screens->diagnostics_items);
    for (uint32_t group = 0; group < DIAGNOSTICS_GROUP_COUNT; group++) {
        screens->diagnostics_entries[group] = (DiagnosticsGroupEntry){screens, group};
        screens->diagnostics_groups[group] =
            (MenuItem){.label = i18n_text(GROUPS[group].title),
                       .select = open_group,
                       .context = &screens->diagnostics_entries[group],
                       .value = NULL,
                       .opens = true};
    }
    app_screens_build_menu(&screens->diagnostics, i18n_text(STR_DIAG_TITLE),
                           screens->diagnostics_groups, DIAGNOSTICS_GROUP_COUNT, theme);
}

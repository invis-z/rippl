#include <string.h>

#include "app_screens.h"
#include "check.h"
#include "default_theme.h"
#include "fake_hal.h"
#include "hal/system.h"

static AppScreens screens;
static AppOptions options;

static MenuConfig all_rows;

static const MenuConfig *built(void)
{
    memset(&screens, 0, sizeof screens);
    diagnostics_menu_build(&screens, &options, &DEFAULT_THEME);
    all_rows = (MenuConfig){.title = "all",
                            .items = screens.diagnostics_items,
                            .item_count = DIAGNOSTICS_ITEM_COUNT,
                            .theme = &DEFAULT_THEME};
    return &all_rows;
}

static const MenuConfig *opened_group(int group)
{
    built();
    const MenuItem *item = &screens.diagnostics.config.items[group];
    ScreenAction action = item->select(item->context);
    CHECK_EQUAL(SCREEN_ACTION_PUSH, action.type);
    return &screens.diagnostics_group.config;
}

static void the_menu_lists_the_groups_in_order(void)
{
    static const char *const GROUPS[] = {"Audio", "Display", "Storage", "USB",
                                         "Power", "System",  "Boot"};
    built();
    const MenuConfig *menu = &screens.diagnostics.config;
    CHECK_EQUAL(7, menu->item_count);
    for (int index = 0; index < menu->item_count; index++) {
        CHECK(strcmp(GROUPS[index], menu->items[index].label) == 0);
        CHECK(menu->items[index].opens);
    }
}

static void each_group_opens_its_own_rows(void)
{
    static const char *const ROWS[][8] = {
        {"Audio output", "Jack 3.5", "Jack 4.4", "Bluetooth", "Fit log", NULL},
        {"Panel", NULL},
        {"SD clock", "SD transfers", NULL},
        {"Last USB", "USB errors", NULL},
        {"Battery", "Last battery", "Power", "Power taps", "Savings", NULL},
        {"Firmware", "CPU clock", "Core 1", "LPC accel", "Clock", "Key test", "Performance"},
        {"Boot hold", "Last start", "Last step", "Last stack", "Stack", "Last update"}};
    int total = 0;
    for (int group = 0; group < 7; group++) {
        const MenuConfig *menu = opened_group(group);
        int count = 0;
        while (count < 7 && ROWS[group][count] != NULL) {
            count++;
        }
        CHECK_EQUAL(count, menu->item_count);
        for (int index = 0; index < count && index < menu->item_count; index++) {
            CHECK(strcmp(ROWS[group][index], menu->items[index].label) == 0);
        }
        total += count;
    }
    CHECK_EQUAL(DIAGNOSTICS_ITEM_COUNT, total);
}

static void every_value_row_reads_without_a_selection(void)
{
    const MenuConfig *menu = built();
    for (int index = 0; index < menu->item_count; index++) {
        const MenuItem *item = &menu->items[index];
        if (!item->opens) {
            CHECK(item->value != NULL);
            CHECK(item->value(item->context) != NULL);
        }
    }
}

static const MenuItem *clock_row(const MenuConfig *menu)
{
    for (int index = 0; index < menu->item_count; index++) {
        if (strcmp(menu->items[index].label, "Clock") == 0) {
            return &menu->items[index];
        }
    }
    return NULL;
}

static void the_clock_row_only_reads_the_clock_and_reads_again_when_pressed(void)
{
    fake_hal_reset();
    const MenuConfig *menu = built();
    const MenuItem *clock = clock_row(menu);
    CHECK(clock != NULL);
    CHECK(strcmp("No reply", clock->value(clock->context)) == 0);
    int reads = fake_hal_time_read_count();
    fake_hal_set_time((HalTime){.present = true,
                                .valid = true,
                                .year = 2026,
                                .month = 11,
                                .day = 5,
                                .hour = 9,
                                .minute = 8,
                                .second = 7});
    CHECK(strcmp("No reply", clock->value(clock->context)) == 0);
    CHECK_EQUAL(SCREEN_ACTION_NONE, clock->select(clock->context).type);
    CHECK(strcmp("2026-11-05 09:08:07", clock->value(clock->context)) == 0);
    CHECK_EQUAL(reads + 1, fake_hal_time_read_count());
}

static const MenuItem *row_named(const MenuConfig *menu, const char *label)
{
    for (int index = 0; index < menu->item_count; index++) {
        if (strcmp(menu->items[index].label, label) == 0) {
            return &menu->items[index];
        }
    }
    return NULL;
}

static void the_savings_row_cycles_the_presets_and_the_power_row_reads_the_hal(void)
{
    fake_hal_reset();
    const MenuConfig *menu = built();
    const MenuItem *savings = row_named(menu, "Savings");
    const MenuItem *power = row_named(menu, "Power");
    CHECK(savings != NULL && power != NULL);
    CHECK(strcmp("All", savings->value(savings->context)) == 0);
    CHECK_EQUAL(SCREEN_ACTION_NONE, savings->select(savings->context).type);
    CHECK(strcmp("No core 1 sleep", savings->value(savings->context)) == 0);
    CHECK_EQUAL(HAL_SAVINGS_DEFAULT & ~HAL_SAVING_CORE1_SLEEP, hal_system_power_savings());
    CHECK(strcmp("24 MHz s0% c0% pdac u0", power->value(power->context)) == 0);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_menu_lists_the_groups_in_order),
        TEST_CASE(each_group_opens_its_own_rows),
        TEST_CASE(the_savings_row_cycles_the_presets_and_the_power_row_reads_the_hal),
        TEST_CASE(every_value_row_reads_without_a_selection),
        TEST_CASE(the_clock_row_only_reads_the_clock_and_reads_again_when_pressed),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}

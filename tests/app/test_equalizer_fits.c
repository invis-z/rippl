#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "check.h"
#include "equalizer_fits.h"
#include "equalizer_store.h"
#include "fake_hal.h"
#include "sound/equalizer_file.h"
#include "sound/equalizer_fit_file.h"
#include "sound/equalizer_text.h"

static const char PRESET_A[] = "Preamp: -3 dB\n"
                               "Filter 1: ON PK Fc 9000 Hz Gain 4 dB Q 1.4\n"
                               "Filter 2: ON PK Fc 120 Hz Gain -3 dB Q 0.9\n";
static const char PRESET_B[] = "Preamp: -2 dB\n"
                               "Filter 1: ON HS Fc 7000 Hz Gain 3 dB Q 0.7\n";
static const char PRESET_C[] = "Preamp: -4 dB\n"
                               "Filter 1: ON PK Fc 6000 Hz Gain 5 dB Q 2.0\n";

typedef struct Saved {
    uint8_t *bytes;
    uint32_t size;
} Saved;

static void add_preset(const char *name, const char *text)
{
    char path[64];
    snprintf(path, sizeof path, "/EQ/%s", name);
    fake_hal_add_directory_entry("/EQ", name, false, false);
    fake_hal_add_file(path, (const uint8_t *)text, (uint32_t)strlen(text));
}

static EqualizerFitsReport refresh(const char *only, bool all, bool log)
{
    return equalizer_fits_refresh((EqualizerFitsProgress){NULL, NULL},
                                  (EqualizerFitsRequest){.only = only, .all = all, .log = log});
}

static uint32_t key_of(const char *text)
{
    EqualizerCurve curve;
    EqualizerTextError error;
    CHECK(equalizer_text_parse(text, (uint32_t)strlen(text), &curve, &error));
    return equalizer_curve_key(&curve);
}

static bool slot_holds(int slot, uint32_t key)
{
    HalFile file;
    EqualizerFitHeader header;
    if (!equalizer_fits_open_slot(&file, slot, &header)) {
        return false;
    }
    hal_file_close(&file);
    return header.key == key && header.entries > 0;
}

static bool slot_empty(int slot)
{
    char path[EQUALIZER_FITS_PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(path, &size);
    bool empty = bytes != NULL && size == EQUALIZER_FITS_SLOT_BYTES;
    for (uint32_t index = 0; empty && index < size; index++) {
        empty = bytes[index] == 0;
    }
    return empty;
}

static Saved save_slot(int slot)
{
    char path[EQUALIZER_FITS_PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(path, &size);
    Saved saved = {malloc(size), size};
    memcpy(saved.bytes, bytes, size);
    return saved;
}

static void restore_slot(int slot, Saved saved)
{
    char path[EQUALIZER_FITS_PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    fake_hal_add_file(path, saved.bytes, saved.size);
    free(saved.bytes);
}

static void slot_names_have_two_digits(void)
{
    char path[EQUALIZER_FITS_PATH_CAPACITY];
    equalizer_fits_slot_path(path, 0);
    CHECK(strcmp("/.rippl/equaliser/E00.BIN", path) == 0);
    equalizer_fits_slot_path(path, 99);
    CHECK(strcmp("/.rippl/equaliser/E99.BIN", path) == 0);
}

static void a_new_preset_is_fitted_into_the_first_slot(void)
{
    fake_hal_reset();
    add_preset("A.txt", PRESET_A);
    EqualizerFitsReport report = refresh(NULL, false, false);
    CHECK_EQUAL(1u, report.presets);
    CHECK_EQUAL(1u, report.fitted);
    CHECK(slot_holds(0, key_of(PRESET_A)));
}

static void a_second_refresh_fits_nothing_and_borrows_nothing(void)
{
    fake_hal_reset();
    add_preset("A.txt", PRESET_A);
    refresh(NULL, false, false);
    int borrows = fake_hal_listing_borrow_count();
    EqualizerFitsReport report = refresh(NULL, false, false);
    CHECK_EQUAL(0u, report.fitted);
    CHECK_EQUAL(borrows, fake_hal_listing_borrow_count());
}

static void a_removed_presets_slot_is_emptied_and_reused(void)
{
    fake_hal_reset();
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    refresh(NULL, false, false);
    CHECK(slot_holds(0, key_of(PRESET_A)));
    CHECK(slot_holds(1, key_of(PRESET_B)));
    Saved first = save_slot(0);
    Saved second = save_slot(1);
    fake_hal_reset();
    restore_slot(0, first);
    restore_slot(1, second);
    add_preset("B.txt", PRESET_B);
    refresh(NULL, false, false);
    CHECK(slot_empty(0));
    CHECK(slot_holds(1, key_of(PRESET_B)));
    add_preset("C.txt", PRESET_C);
    EqualizerFitsReport report = refresh(NULL, false, false);
    CHECK_EQUAL(1u, report.fitted);
    CHECK(slot_holds(0, key_of(PRESET_C)));
    CHECK(slot_holds(1, key_of(PRESET_B)));
}

static void refitting_one_preset_keeps_its_slot_and_the_others(void)
{
    fake_hal_reset();
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    refresh(NULL, false, false);
    EqualizerFitsReport report = refresh("B.txt", false, false);
    CHECK_EQUAL(1u, report.fitted);
    CHECK(slot_holds(0, key_of(PRESET_A)));
    CHECK(slot_holds(1, key_of(PRESET_B)));
    report = refresh(NULL, true, false);
    CHECK_EQUAL(2u, report.fitted);
    CHECK(slot_holds(0, key_of(PRESET_A)));
    CHECK(slot_holds(1, key_of(PRESET_B)));
}

static void the_fit_log_is_written_only_when_asked(void)
{
    fake_hal_reset();
    add_preset("A.txt", PRESET_A);
    refresh(NULL, false, false);
    uint32_t size = 0;
    CHECK(fake_hal_file_bytes("/.rippl/equaliser/FIT.LOG", &size) == NULL);
    refresh(NULL, true, true);
    const uint8_t *bytes = fake_hal_file_bytes("/.rippl/equaliser/FIT.LOG", &size);
    CHECK(bytes != NULL);
    char text[4096] = {0};
    memcpy(text, bytes, size < sizeof text - 1 ? size : sizeof text - 1);
    CHECK(strstr(text, " Hz: ") != NULL);
    CHECK(strstr(text, "A.txt -> /.rippl/equaliser/E00.BIN: ") != NULL);
}

static EqualizerCurve curve_of(const char *text)
{
    EqualizerCurve curve;
    EqualizerTextError error;
    CHECK(equalizer_text_parse(text, (uint32_t)strlen(text), &curve, &error));
    return curve;
}

static int mapped(uint32_t key)
{
    uint32_t size = 0;
    const uint8_t *bytes = fake_hal_file_bytes(EQUALIZER_STORE_PATH, &size);
    if (bytes == NULL || size < EQUALIZER_FILE_WITH_MAP) {
        return EQUALIZER_MAP_UNKNOWN;
    }
    return equalizer_map_find(bytes + EQUALIZER_MAP_OFFSET, key);
}

static void list_slot(int slot)
{
    char path[EQUALIZER_FITS_PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    fake_hal_add_directory_entry("/.rippl/equaliser", path + strlen("/.rippl/equaliser/"), false,
                                 false);
}

static void write_slot(int slot, Saved saved)
{
    char path[EQUALIZER_FITS_PATH_CAPACITY];
    equalizer_fits_slot_path(path, slot);
    CHECK(hal_file_write(path, saved.bytes, saved.size));
    free(saved.bytes);
}

static void start_with_store(void)
{
    static const uint8_t empty[4096];
    fake_hal_reset();
    fake_hal_add_file(EQUALIZER_STORE_PATH, empty, sizeof empty);
}

static void the_refresh_maps_each_fitted_key_to_its_slot(void)
{
    start_with_store();
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    refresh(NULL, false, false);
    int a = mapped(key_of(PRESET_A));
    int b = mapped(key_of(PRESET_B));
    CHECK(a >= 0 && b >= 0 && a != b);
    CHECK(slot_holds(a, key_of(PRESET_A)));
    CHECK(slot_holds(b, key_of(PRESET_B)));
    CHECK_EQUAL(EQUALIZER_MAP_ABSENT, mapped(key_of(PRESET_C)));
}

static void a_mapped_preset_is_applied_without_a_search(void)
{
    start_with_store();
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    refresh(NULL, false, false);
    int opens = fake_hal_dir_open_count();
    EqualizerCurve curve = curve_of(PRESET_B);
    CHECK_EQUAL((uint32_t)mapped(key_of(PRESET_B)) + 1u, equalizer_fits_apply(&curve));
    CHECK_EQUAL(opens, fake_hal_dir_open_count());
}

static void a_preset_missing_from_the_map_gets_no_fit_and_no_search(void)
{
    start_with_store();
    add_preset("A.txt", PRESET_A);
    refresh(NULL, false, false);
    int opens = fake_hal_dir_open_count();
    EqualizerCurve curve = curve_of(PRESET_C);
    CHECK_EQUAL(0u, equalizer_fits_apply(&curve));
    CHECK_EQUAL(opens, fake_hal_dir_open_count());
}

static void without_a_map_the_fit_is_searched_for(void)
{
    fake_hal_reset();
    add_preset("A.txt", PRESET_A);
    refresh(NULL, false, false);
    list_slot(0);
    int opens = fake_hal_dir_open_count();
    EqualizerCurve curve = curve_of(PRESET_A);
    CHECK_EQUAL(1u, equalizer_fits_apply(&curve));
    CHECK_EQUAL(opens + 1, fake_hal_dir_open_count());
}

static void a_mapped_slot_that_fails_its_check_is_searched_for(void)
{
    start_with_store();
    add_preset("A.txt", PRESET_A);
    add_preset("B.txt", PRESET_B);
    refresh(NULL, false, false);
    int a = mapped(key_of(PRESET_A));
    int b = mapped(key_of(PRESET_B));
    list_slot(a);
    list_slot(b);
    Saved first = save_slot(a);
    Saved second = save_slot(b);
    write_slot(a, second);
    write_slot(b, first);
    int opens = fake_hal_dir_open_count();
    EqualizerCurve curve = curve_of(PRESET_A);
    CHECK_EQUAL((uint32_t)b + 1u, equalizer_fits_apply(&curve));
    CHECK_EQUAL(opens + 1, fake_hal_dir_open_count());
}

static void saving_the_choice_keeps_the_map(void)
{
    start_with_store();
    add_preset("A.txt", PRESET_A);
    refresh(NULL, false, false);
    int a = mapped(key_of(PRESET_A));
    EqualizerStore store;
    equalizer_store_load(&store);
    store.choice.preset = EQUALIZER_PRESET_FILE;
    strcpy(store.choice.preset_name, "A.txt");
    CHECK(equalizer_store_save(&store));
    CHECK(a >= 0);
    CHECK_EQUAL(a, mapped(key_of(PRESET_A)));
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(slot_names_have_two_digits),
        TEST_CASE(a_new_preset_is_fitted_into_the_first_slot),
        TEST_CASE(a_second_refresh_fits_nothing_and_borrows_nothing),
        TEST_CASE(a_removed_presets_slot_is_emptied_and_reused),
        TEST_CASE(refitting_one_preset_keeps_its_slot_and_the_others),
        TEST_CASE(the_fit_log_is_written_only_when_asked),
        TEST_CASE(the_refresh_maps_each_fitted_key_to_its_slot),
        TEST_CASE(a_mapped_preset_is_applied_without_a_search),
        TEST_CASE(a_preset_missing_from_the_map_gets_no_fit_and_no_search),
        TEST_CASE(without_a_map_the_fit_is_searched_for),
        TEST_CASE(a_mapped_slot_that_fails_its_check_is_searched_for),
        TEST_CASE(saving_the_choice_keeps_the_map),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}

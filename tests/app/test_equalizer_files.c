#include "check.h"
#include "equalizer_files.h"
#include "fake_hal.h"

#include <stdio.h>
#include <string.h>

static bool has_name(const EqualizerFileList *list, const char *name)
{
    for (int index = 0; index < list->count; index++) {
        char full[EQUALIZER_FILES_NAME_CAPACITY];
        equalizer_files_name(list, index, full);
        if (strcmp(full, name) == 0) {
            return true;
        }
    }
    return false;
}

static void the_scan_keeps_txt_files_and_drops_other_entries(void)
{
    EqualizerFileList list;
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "b.TXT", false, false);
    fake_hal_add_directory_entry("/EQ", "a.txt", false, false);
    fake_hal_add_directory_entry("/EQ", "notes.mp3", false, false);
    fake_hal_add_directory_entry("/EQ", "album", true, false);
    fake_hal_add_directory_entry("/EQ", ".hidden.txt", false, true);
    fake_hal_add_directory_entry("/EQ", "._a.txt", false, false);
    equalizer_files_scan(&list);
    CHECK_EQUAL(2, list.count);
    CHECK(has_name(&list, "a.txt"));
    CHECK(has_name(&list, "b.TXT"));
}

static void the_scan_stops_at_a_hundred_files(void)
{
    EqualizerFileList list;
    fake_hal_reset();
    for (int index = 1; index <= EQUALIZER_FILES_MAX + 1; index++) {
        char name[16];
        snprintf(name, sizeof name, "%d.txt", index);
        fake_hal_add_directory_entry("/EQ", name, false, false);
    }
    equalizer_files_scan(&list);
    CHECK_EQUAL(EQUALIZER_FILES_MAX, list.count);
    CHECK_EQUAL(EQUALIZER_FILES_MAX, fake_hal_dir_next_count());
    CHECK(has_name(&list, "100.txt"));
    CHECK(!has_name(&list, "101.txt"));
}

static void a_name_is_found_by_its_place_in_the_folder(void)
{
    EqualizerFileList list;
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "notes.mp3", false, false);
    fake_hal_add_directory_entry("/EQ", "a.txt", false, false);
    fake_hal_add_directory_entry("/EQ", "b.txt", false, false);
    equalizer_files_scan(&list);
    CHECK_EQUAL(1, equalizer_files_find(&list, "b.txt"));
    CHECK_EQUAL(-1, equalizer_files_find(&list, "c.txt"));
    char name[EQUALIZER_FILES_NAME_CAPACITY];
    CHECK(!equalizer_files_name(&list, 2, name));
}

static void the_list_shows_names_without_the_extension(void)
{
    EqualizerFileList list;
    fake_hal_reset();
    fake_hal_add_directory_entry("/EQ", "Sennheiser HD 650.v2.TXT", false, false);
    equalizer_files_scan(&list);
    CHECK_EQUAL(1, list.count);
    char stem[EQUALIZER_PRESET_NAME_CAPACITY];
    CHECK(equalizer_files_stem(&list, 0, stem));
    CHECK(strcmp("Sennheiser HD 650.v2", stem) == 0);
    CHECK(has_name(&list, "Sennheiser HD 650.v2.TXT"));
    CHECK_EQUAL(20, equalizer_files_stem_length("Sennheiser HD 650.v2.TXT"));
    CHECK_EQUAL(4, equalizer_files_stem_length("Bass"));
}

static void a_missing_folder_lists_nothing(void)
{
    EqualizerFileList list;
    fake_hal_reset();
    equalizer_files_scan(&list);
    CHECK_EQUAL(0, list.count);
}

int main(void)
{
    static const TestCase cases[] = {
        TEST_CASE(the_scan_keeps_txt_files_and_drops_other_entries),
        TEST_CASE(the_scan_stops_at_a_hundred_files),
        TEST_CASE(a_name_is_found_by_its_place_in_the_folder),
        TEST_CASE(the_list_shows_names_without_the_extension),
        TEST_CASE(a_missing_folder_lists_nothing),
    };
    return test_run_all(cases, TEST_CASE_COUNT(cases));
}

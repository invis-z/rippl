#include "app/app.h"

#include "app_state.h"
#include "core/resident.h"

static void number_text(char *text, size_t capacity, uint32_t value);

static const char DEBUG_FILE_PATH[] = "/" VOLUME_INTERNAL_NAME "/debug";

void app_check_debug_file(void)
{
    HalFile file;
    bool found = hal_file_open(&file, DEBUG_FILE_PATH);
    if (found) {
        hal_file_close(&file);
    }
    app.screens.diagnostics_gate.debug_file = found;
}

RIPPL_STARTUP_CODE void app_report_card_files(CardFilesReport files, uint32_t now_ms)
{
    if (files.card_full) {
        toast_show(&app.toast, i18n_text(STR_BOOT_CARD_FULL), now_ms);
    }
    if (files.created && !segment_store_found(&app.library.segments)) {
        app.rescan_requested = true;
    } else if (music_check_firmware_is_new()) {
        app.music_check_requested = true;
        app.clock_question = true;
    }
}

static void show_fit_progress(void *context, uint32_t preset, uint32_t presets, uint32_t step,
                              uint32_t steps)
{
    (void)context;
    char text[PROGRESS_TEXT_CAPACITY];
    char done[NUMBER_CAPACITY];
    char total[NUMBER_CAPACITY];
    number_text(done, sizeof done, preset);
    number_text(total, sizeof total, presets);
    const char *arguments[] = {done, total};
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format(&builder, STR_SETTINGS_EQ_PREPARING, arguments, 2);
    app_show_progress(i18n_text(STR_SETTINGS_EQUALIZER), text, step, steps);
}

static EqualizerFitsReport fit_equalizer_presets(const char *only, bool all)
{
    EqualizerFitsReport report = {0, 0, 0};
    const EqualizerFitsEntry *entry = hal_system_load_fit_code();
    if (entry == NULL || entry->magic != EQUALIZER_FITS_MAGIC) {
        return report;
    }
    EqualizerFitsRequest request = {.only = only, .all = all, .log = app.options.log_fits};
    report = entry->refresh((EqualizerFitsProgress){show_fit_progress, NULL}, request);
    if (report.fitted + report.failed > 0) {
        app_show_notice(i18n_text(STR_SETTINGS_EQUALIZER), i18n_text(STR_RESCAN_CHECKING));
        app_wake();
    }
    return report;
}

void app_prepare_equalizer_fits(void)
{
    fit_equalizer_presets(NULL, false);
}

bool app_refit_equalizer_preset(const char *name)
{
    if (player_status(&app.player).state == PLAYER_PLAYING) {
        player_toggle_pause(&app.player);
    }
    player_release_output(&app.player);
    EqualizerFitsReport report = fit_equalizer_presets(name, name == NULL);
    equalizer_store_apply(&app.equalizer);
    app_wake();
    return report.fitted > 0;
}

void app_show_card_setup(void)
{
    app_show_notice("Rippl", i18n_text(STR_BOOT_CARD_SETUP));
}

static void number_text(char *text, size_t capacity, uint32_t value)
{
    StringBuilder builder = string_builder_make(text, capacity);
    string_builder_append_unsigned(&builder, value);
}

static void show_rescan_progress(uint32_t files, uint32_t tracks)
{
    static uint32_t shown_ms;
    uint32_t now_ms = hal_clock_milliseconds();
    if (files > 1 && now_ms - shown_ms < RESCAN_PROGRESS_MS) {
        return;
    }
    shown_ms = now_ms;
    static char text[PROGRESS_TEXT_CAPACITY];
    char count[NUMBER_CAPACITY];
    number_text(count, sizeof count, tracks);
    const char *arguments[] = {count};
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format(&builder, STR_RESCAN_TRACKS_SO_FAR, arguments, 1);
    app_show_notice(i18n_text(STR_RESCAN_TITLE), text);
}

static void show_cover_progress(uint32_t done, uint32_t total)
{
    static uint32_t shown_ms;
    uint32_t now_ms = hal_clock_milliseconds();
    if (done > 0 && done < total && now_ms - shown_ms < RESCAN_PROGRESS_MS) {
        return;
    }
    shown_ms = now_ms;
    static char text[PROGRESS_TEXT_CAPACITY];
    char finished[NUMBER_CAPACITY];
    char albums[NUMBER_CAPACITY];
    number_text(finished, sizeof finished, done);
    number_text(albums, sizeof albums, total);
    const char *arguments[] = {finished, albums};
    StringBuilder builder = string_builder_make(text, sizeof text);
    i18n_format(&builder, STR_RESCAN_ALBUMS_PROGRESS, arguments, 2);
    app_show_notice(i18n_text(STR_RESCAN_COVERS_TITLE), text);
}

void app_explain_refusals(uint32_t now_ms)
{
    static char text[MESSAGE_TEXT_CAPACITY];
    uint32_t count = app.player.refusal_count;
    bool fresh = count > app.shown_refusals;
    app.shown_refusals = count;
    if (fresh) {
        player_refusal_text(app.player.refusal, text, sizeof text);
        toast_show(&app.toast, text, now_ms);
    }
}

void app_run_rescan(void)
{
    app_show_notice(i18n_text(STR_RESCAN_TITLE), i18n_text(STR_RESCAN_STARTING));
    RescanProgress progress = {show_rescan_progress, show_cover_progress};
    RescanOutcome outcome = library_rescan_run(&app.library, progress);
    static char message[MESSAGE_TEXT_CAPACITY];
    const char *text = library_rescan_message(outcome, message, sizeof message);
    if (outcome.result == SCAN_OK) {
        music_check_remember(music_check_fingerprint());
        app_prepare_equalizer_fits();
        app_start();
    }
    app_wake();
    toast_show(&app.toast, text, hal_clock_milliseconds());
}

void app_request_rescan(void)
{
    app.rescan_requested = true;
}

void app_request_rescan_if_music_changed(void)
{
    app.music_check_requested = true;
    app.usb_written = true;
}

#include "overlay.h"

#include <stddef.h>
#include <stdint.h>

#include "board.h"
#include "core/bytes.h"
#include "device/image_crc.h"
#include "device/np_faces_gate.h"
#include "emmc.h"
#include "hal/system.h"

#define CODE_BASE 0x03050000u

enum {
    SECTOR_BYTES = 512,
    SECTORS_PER_READ = 8
};

extern const uint8_t device_code_end[];

static NpFacesGate np_faces_gate;
static bool startup_running;
static uint32_t asset_wipes;
static uint32_t scene_wipes;

static const BoardOverlay NP_FACES_OVERLAY = {NP_FACES_IMAGE_OFFSET, NP_FACES_IMAGE_MAGIC,
                                              (uint8_t *)NP_FACES_BASE, NP_FACES_BYTES};

static const BoardOverlay ENGINE_OVERLAY = {ENGINE_IMAGE_OFFSET, ENGINE_IMAGE_MAGIC,
                                            (uint8_t *)ENGINE_BASE, ENGINE_BYTES};

static const BoardOverlay STARTUP_OVERLAY = {STARTUP_IMAGE_OFFSET, STARTUP_IMAGE_MAGIC,
                                             (uint8_t *)NP_FACES_BASE, NP_FACES_BYTES};

static uint32_t running_code_crc(void)
{
    static uint32_t crc;
    static bool known;
    if (!known) {
        const uint8_t *code = (const uint8_t *)(uintptr_t)CODE_BASE;
        crc = image_crc_update(0, code, (uint32_t)((uintptr_t)device_code_end - CODE_BASE));
        known = true;
    }
    return crc;
}

static bool read_sectors(uint32_t first_sector, uint32_t sectors, uint8_t *target)
{
    for (uint32_t done = 0; done < sectors; done += SECTORS_PER_READ) {
        uint32_t count = sectors - done < SECTORS_PER_READ ? sectors - done : SECTORS_PER_READ;
        if (!emmc_read(NULL, first_sector + done, count, target + done * SECTOR_BYTES)) {
            return false;
        }
    }
    return true;
}

static uint32_t header_size(const BoardOverlay *overlay)
{
    const uint8_t *base = overlay->base;
    uint32_t size = bytes_get32(base + 4);
    uint32_t sectors = (size + SECTOR_BYTES - 1) / SECTOR_BYTES;
    if (bytes_get32(base) != overlay->magic || size < OVERLAY_HEADER_BYTES ||
        sectors * SECTOR_BYTES > overlay->capacity ||
        bytes_get32(base + 12) != running_code_crc()) {
        return 0;
    }
    return size;
}

static bool body_intact(const BoardOverlay *overlay, uint32_t size)
{
    const uint8_t *base = overlay->base;
    return image_crc_update(0, base + OVERLAY_HEADER_BYTES, size - OVERLAY_HEADER_BYTES) ==
           bytes_get32(base + 8);
}

static uint32_t already_loaded(const BoardOverlay *overlay)
{
    uint32_t size = header_size(overlay);
    return size != 0 && body_intact(overlay, size) ? size : 0;
}

static uint32_t load_from(uint32_t slot, const BoardOverlay *overlay)
{
    uint32_t first_sector = slot + overlay->image_offset / SECTOR_BYTES;
    if (!emmc_read(NULL, first_sector, 1, overlay->base)) {
        return 0;
    }
    uint32_t size = header_size(overlay);
    uint32_t sectors = (size + SECTOR_BYTES - 1) / SECTOR_BYTES;
    if (size == 0 || !read_sectors(first_sector + 1, sectors - 1, overlay->base + SECTOR_BYTES) ||
        !body_intact(overlay, size)) {
        return 0;
    }
    return size;
}

static bool installed_header_matches(const BoardOverlay *overlay, uint32_t slot)
{
    static uint32_t sector[SECTOR_BYTES / 4];
    if (!emmc_read(NULL, slot + overlay->image_offset / SECTOR_BYTES, 1, sector)) {
        return false;
    }
    const uint8_t *installed = (const uint8_t *)sector;
    for (uint32_t index = 0; index < OVERLAY_HEADER_BYTES; index++) {
        if (installed[index] != overlay->base[index]) {
            return false;
        }
    }
    return true;
}

bool board_overlay_load(const BoardOverlay *overlay)
{
    EmmcInfo info;
    bool storage = emmc_start(&info);
    uint32_t size = already_loaded(overlay);
    if (size != 0 && storage && !installed_header_matches(overlay, info.layout.slot_1)) {
        size = 0;
    }
    if (size == 0 && storage) {
        size = load_from(info.layout.slot_1, overlay);
        size = size != 0 ? size : load_from(info.layout.slot_2, overlay);
    }
    if (size == 0) {
        return false;
    }
    for (uint32_t index = size; index < overlay->capacity; index++) {
        overlay->base[index] = 0;
    }
    __asm__ volatile("dsb\n\tisb" ::: "memory");
    return true;
}

static const void *load_window(uint32_t image_offset, uint32_t magic, uint32_t capacity)
{
    uint8_t *base = (uint8_t *)(uintptr_t)OVERLAY_WINDOW_BASE;
    BoardOverlay overlay = {image_offset, magic, base, capacity};
    if (startup_running) {
        return NULL;
    }
    board_np_faces_forget();
    board_theme_assets_forget();
    board_audio_shut_down();
    return board_overlay_load(&overlay) ? base + OVERLAY_HEADER_BYTES : NULL;
}

const void *hal_system_load_rescan_code(void)
{
    return load_window(RESCAN_OVERLAY_IMAGE_OFFSET, RESCAN_OVERLAY_MAGIC, OVERLAY_WINDOW_BYTES);
}

const void *hal_system_load_fit_code(void)
{
    return load_window(FIT_OVERLAY_IMAGE_OFFSET, FIT_OVERLAY_MAGIC, OVERLAY_WINDOW_BYTES);
}

void board_np_faces_forget(void)
{
    np_faces_gate_forget(&np_faces_gate);
    scene_wipes++;
}

void *hal_system_scene_memory(uint32_t *size)
{
    uint32_t loaded = header_size(&NP_FACES_OVERLAY);
    uint32_t used = (loaded + SECTOR_BYTES - 1) / SECTOR_BYTES * SECTOR_BYTES;
    bool ready = !startup_running && np_faces_gate.state == NP_FACES_READY && loaded != 0 &&
                 used < NP_FACES_BYTES;
    uint8_t *memory = ready ? (uint8_t *)NP_FACES_BASE + used : NULL;
    __asm__("" : "+r"(memory));
    *size = ready ? NP_FACES_BYTES - used : 0;
    return memory;
}

uint32_t hal_system_scene_wipes(void)
{
    return scene_wipes;
}

void board_theme_assets_forget(void)
{
    asset_wipes++;
}

uint32_t hal_system_theme_asset_wipes(void)
{
    return asset_wipes;
}

static bool np_faces_header_intact(void)
{
    const uint8_t *base = (const uint8_t *)(uintptr_t)NP_FACES_BASE;
    return bytes_get32(base) == NP_FACES_IMAGE_MAGIC &&
           bytes_get32(base + 12) == running_code_crc();
}

static bool np_faces_may_load(void)
{
    uint32_t status;
    return !hal_system_restart_pending() && emmc_status(&status);
}

bool board_startup_keep(void)
{
    board_np_faces_forget();
    return board_overlay_load(&STARTUP_OVERLAY);
}

__attribute__((noinline)) const void *hal_system_load_startup(void)
{
    startup_running = board_startup_keep();
    const void *entry = (const void *)(uintptr_t)NP_FACES_ENTRY;
    __asm__("" : "+r"(entry));
    return startup_running ? entry : NULL;
}

void hal_system_release_startup(void)
{
    startup_running = false;
}

__attribute__((noinline)) const void *hal_system_load_faces(void)
{
    if (startup_running || !hal_system_code2_ready()) {
        return NULL;
    }
    if (np_faces_gate.state == NP_FACES_READY) {
        np_faces_gate_checked(&np_faces_gate, np_faces_header_intact());
    }
    if (np_faces_gate.state == NP_FACES_UNKNOWN &&
        np_faces_gate_should_load(&np_faces_gate, np_faces_may_load())) {
        np_faces_gate_loaded(&np_faces_gate, board_overlay_load(&NP_FACES_OVERLAY));
        scene_wipes++;
    }
    const void *entry = (const void *)(uintptr_t)NP_FACES_ENTRY;
    __asm__("" : "+r"(entry));
    return np_faces_gate.state == NP_FACES_READY ? entry : NULL;
}

const void *board_usb_overlay_load(void)
{
    return load_window(USB_OVERLAY_IMAGE_OFFSET, USB_OVERLAY_MAGIC, USB_OVERLAY_WINDOW_BYTES);
}

__attribute__((noinline)) const void *hal_system_load_theme_engine(void)
{
    const void *entry = (const void *)(uintptr_t)(ENGINE_BASE + OVERLAY_HEADER_BYTES);
    __asm__("" : "+r"(entry));
    return board_overlay_load(&ENGINE_OVERLAY) ? entry : NULL;
}

#include "sd.h"

#include <stddef.h>

#include "board.h"
#include "cru.h"
#include "device/card_write.h"
#include "device/sd_card.h"
#include "gpio.h"
#include "hal/clock.h"
#include "mmc.h"
#include "registers.h"

#define CRU_SD_IDENTIFY 0x009F009Fu
#define CRU_SD_TRANSFER 0x009F0080u
#define GRF_SD_MUX_LOW 0x20u
#define GRF_SD_MUX_LOW_VALUE 0xFC005400u
#define GRF_SD_MUX_HIGH 0x24u
#define GRF_SD_MUX_HIGH_VALUE 0x003F0015u
#define GRF_DETECT_MUX 0x04u
#define GRF_DETECT_MUX_VALUE 0x00C00000u
#define GRF_DETECT_PULL 0x14u
#define GRF_DETECT_PULL_VALUE 0x00080008u
#define CRU_MMC_DIVIDERS 0x30u
#define CRU_SD_PLL_MASK 0x009F0000u
#define SWITCH_CHECK_HIGH_SPEED 0x00FFFFF1u
#define SWITCH_SET_HIGH_SPEED 0x80FFFFF1u

enum {
    GATE_SD_BUS = 0x18,
    GATE_SD_CARD_CLOCK = 0x6B,
    RESET_SD = 0x12,
    POWER_SETTLE_MS = 1,
    DETECT_SETTLE_MS = 1,
    CRYSTAL_MHZ = 24,
    HIGH_SPEED_MHZ = 50,
    SWITCH_STATUS_WORDS = 16,
    SWITCH_SUPPORT_BYTE = 13,
    SWITCH_HIGH_SPEED_BIT = 0x02,
    SWITCH_RESULT_BYTE = 16,
    SWITCH_RESULT_MASK = 0x0F,
    SWITCH_HIGH_SPEED = 1,
    SD_DMA_REQUEST = 12
};

static const GpioPin CARD_DETECT = {0, 11};
static uint32_t sd_fifo_depth;
static const MmcHost SD_HOST = {SD_BASE, CRU_SD_IDENTIFY, CRU_SD_TRANSFER, &sd_fifo_depth};

typedef struct CardTransfer {
    uint32_t lba;
    uint32_t count;
    uint32_t *words;
    const uint32_t *source;
} CardTransfer;

static SdCard card;
static bool ready;
static SdHealth health;
static SdSpeed speed;
static bool fast_clock;
static MmcAsync async_state;
static CardTransfer async_transfer;

static bool host_command(void *context, SdCommand command, uint32_t response[4])
{
    (void)context;
    return mmc_command(&SD_HOST, command, response);
}

static void host_set_clock(void *context, SdClock clock)
{
    (void)context;
    mmc_set_clock(&SD_HOST, clock);
}

static void host_set_bus_width(void *context, uint8_t bits)
{
    (void)context;
    mmc_set_bus_width(&SD_HOST, bits);
}

static void host_delay(void *context, uint32_t milliseconds)
{
    (void)context;
    board_delay_ms(milliseconds);
}

static bool host_card_present(void *context)
{
    (void)context;
    return !gpio_read(CARD_DETECT);
}

static void host_stop(void *context)
{
    (void)context;
    cru_gate(GATE_SD_CARD_CLOCK, true);
    mmc_stop_clock(&SD_HOST);
    cru_gate(GATE_SD_CARD_CLOCK, false);
    cru_gate(GATE_SD_BUS, false);
}

static uint32_t host_now_ms(void *context)
{
    (void)context;
    return hal_clock_milliseconds();
}

static const SdHost HOST = {host_command,      host_set_clock, host_set_bus_width, host_delay,
                            host_card_present, host_stop,      host_now_ms,        NULL};

static void start_card_detect(void)
{
    register_write(GRF_BASE + GRF_DETECT_MUX, GRF_DETECT_MUX_VALUE);
    gpio_input(CARD_DETECT);
    register_write(GRF_BASE + GRF_DETECT_PULL, GRF_DETECT_PULL_VALUE);
    board_delay_ms(DETECT_SETTLE_MS);
}

static void power_controller(void)
{
    cru_gate(GATE_SD_BUS, true);
    cru_gate(GATE_SD_CARD_CLOCK, true);
    cru_reset(RESET_SD, false);
    cru_write(CRU_MMC_DIVIDERS, CRU_SD_IDENTIFY);
    register_write(GRF_BASE + GRF_SD_MUX_LOW, GRF_SD_MUX_LOW_VALUE);
    register_write(GRF_BASE + GRF_SD_MUX_HIGH, GRF_SD_MUX_HIGH_VALUE);
    board_delay_ms(POWER_SETTLE_MS);
    mmc_reset_controller(&SD_HOST);
    mmc_set_clock(&SD_HOST, SD_CLOCK_IDENTIFY);
    board_delay_ms(POWER_SETTLE_MS);
}

static bool start_card(void)
{
    start_card_detect();
    if (!host_card_present(NULL)) {
        return false;
    }
    power_controller();
    bool started = sd_card_init(&card, &HOST);
    if (started) {
        health.starts++;
    }
    return started;
}

static bool in_transfer_state(void)
{
    return sd_card_in_transfer(&HOST, &card);
}

static bool wait_for_transfer_state(void)
{
    return sd_card_wait_transfer(&HOST, &card);
}

static uint32_t fast_cru(void)
{
    uint32_t mhz = hal_system_clock_mhz();
    if (mhz <= CRYSTAL_MHZ) {
        return CRU_SD_TRANSFER;
    }
    uint32_t divider = (mhz + HIGH_SPEED_MHZ - 1) / HIGH_SPEED_MHZ - 1;
    return CRU_SD_PLL_MASK | divider;
}

static void use_fast_clock(bool fast)
{
    mmc_set_divider(&SD_HOST, fast ? fast_cru() : CRU_SD_TRANSFER);
    fast_clock = fast;
}

static void fall_back(void)
{
    if (speed == SD_SPEED_HIGH) {
        speed = SD_SPEED_FELL_BACK;
    }
    if (fast_clock) {
        use_fast_clock(false);
    }
}

static void note_card_failure(void)
{
    health.failure = mmc_last_failure();
    health.failures++;
    fall_back();
}

static uint8_t status_byte(const uint32_t *words, uint32_t index)
{
    return (uint8_t)(words[index / 4] >> (8 * (index % 4)));
}

static bool switch_to_high_speed(void)
{
    uint32_t status[SWITCH_STATUS_WORDS];
    if (!mmc_read_switch_status(&SD_HOST, SWITCH_CHECK_HIGH_SPEED, status) ||
        (status_byte(status, SWITCH_SUPPORT_BYTE) & SWITCH_HIGH_SPEED_BIT) == 0) {
        return false;
    }
    return mmc_read_switch_status(&SD_HOST, SWITCH_SET_HIGH_SPEED, status) &&
           (status_byte(status, SWITCH_RESULT_BYTE) & SWITCH_RESULT_MASK) == SWITCH_HIGH_SPEED;
}

static void finish_async(void);

static void try_high_speed(void)
{
    fast_clock = false;
    if (speed != SD_SPEED_DEFAULT && speed != SD_SPEED_HIGH) {
        return;
    }
    if (!switch_to_high_speed()) {
        speed = SD_SPEED_REFUSED;
        return;
    }
    use_fast_clock(true);
    speed = SD_SPEED_HIGH;
}

SdSpeed sd_speed(void)
{
    return speed;
}

void sd_before_clock_change(void)
{
    finish_async();
    if (fast_clock) {
        mmc_set_divider(&SD_HOST, CRU_SD_TRANSFER);
    }
}

void sd_after_clock_change(void)
{
    if (fast_clock) {
        mmc_set_divider(&SD_HOST, fast_cru());
    }
}

static bool resume_card(void *context)
{
    (void)context;
    if (in_transfer_state()) {
        return true;
    }
    mmc_stop(&SD_HOST, sd_card_rca_argument(&card));
    return wait_for_transfer_state();
}

static bool restart_card(void *context)
{
    (void)context;
    fall_back();
    fast_clock = false;
    ready = start_card();
    return ready;
}

static bool read_now(void *context)
{
    const CardTransfer *transfer = context;
    bool read = mmc_read_blocks(&SD_HOST, sd_card_address(&card, transfer->lba), transfer->count,
                                sd_card_rca_argument(&card), transfer->words);
    if (!read) {
        note_card_failure();
    }
    return read;
}

static bool fits(uint32_t lba, uint32_t count)
{
    return ready && count > 0 && lba < card.sector_count && count <= card.sector_count - lba;
}

static void finish_async(void)
{
    if (async_state != MMC_ASYNC_PENDING) {
        return;
    }
    while (sd_read_poll() == MMC_ASYNC_PENDING) {
    }
}

uint32_t sd_read_start(uint32_t lba, uint32_t count, void *buffer)
{
    finish_async();
    count = count < MMC_ASYNC_MAX_BLOCKS ? count : MMC_ASYNC_MAX_BLOCKS;
    if (!fits(lba, count) ||
        !mmc_read_start(&SD_HOST, sd_card_address(&card, lba), count, buffer, SD_DMA_REQUEST)) {
        return 0;
    }
    async_transfer = (CardTransfer){lba, count, buffer, NULL};
    async_state = MMC_ASYNC_PENDING;
    return count;
}

MmcAsync sd_read_poll(void)
{
    if (async_state != MMC_ASYNC_PENDING) {
        MmcAsync result = async_state;
        async_state = MMC_ASYNC_IDLE;
        return result;
    }
    MmcAsync result = mmc_read_poll(&SD_HOST, sd_card_rca_argument(&card));
    if (result == MMC_ASYNC_PENDING) {
        return result;
    }
    if (result != MMC_ASYNC_DONE) {
        health.dma_failures++;
        note_card_failure();
        SdRetry retry = {read_now, resume_card, restart_card, &async_transfer};
        result = sd_card_retry(&retry, &health.retry) ? MMC_ASYNC_DONE : MMC_ASYNC_FAILED;
    }
    async_state = result;
    return result;
}

bool sd_read(void *context, uint32_t lba, uint32_t count, void *buffer)
{
    (void)context;
    finish_async();
    if (!fits(lba, count)) {
        return false;
    }
    CardTransfer transfer = {lba, count, buffer, NULL};
    SdRetry retry = {read_now, resume_card, restart_card, &transfer};
    return sd_card_retry(&retry, &health.retry);
}

bool sd_start(BlockDevice *device)
{
    if (!ready) {
        ready = start_card();
        if (ready) {
            try_high_speed();
        }
    }
    *device = (BlockDevice){sd_read, NULL, sd_write};
    return ready;
}

bool sd_prepare(BlockDevice *device)
{
    finish_async();
    if (ready && (!host_card_present(NULL) || !resume_card(NULL))) {
        ready = false;
    }
    return sd_start(device);
}

SdHealth sd_health(void)
{
    return health;
}

static bool write_data(void *context, uint8_t command, uint32_t address, uint32_t count,
                       const uint32_t *words)
{
    (void)context;
    return mmc_write_blocks_dma(&SD_HOST, command, address, count, words, SD_DMA_REQUEST);
}

static bool stop_writing(void *context)
{
    (void)context;
    return mmc_stop(&SD_HOST, sd_card_rca_argument(&card));
}

static bool wait_until_ready(void *context)
{
    (void)context;
    if (wait_for_transfer_state()) {
        return true;
    }
    mmc_note_failure(&SD_HOST, MMC_STEP_TRANSFER_STATE);
    return false;
}

static const CardWriter WRITER = {write_data, stop_writing, wait_until_ready, NULL};

static bool write_now(void *context)
{
    const CardTransfer *transfer = context;
    bool written = card_write(&WRITER, sd_card_address(&card, transfer->lba), transfer->count,
                              transfer->source);
    if (!written) {
        health.dma_failures++;
        note_card_failure();
    }
    return written;
}

bool sd_write(void *context, uint32_t lba, uint32_t count, const void *buffer)
{
    (void)context;
    finish_async();
    if (!fits(lba, count)) {
        return false;
    }
    CardTransfer transfer = {lba, count, NULL, buffer};
    SdRetry retry = {write_now, resume_card, restart_card, &transfer};
    return sd_card_retry(&retry, &health.retry);
}

uint32_t sd_sector_count(void)
{
    return ready ? card.sector_count : 0;
}

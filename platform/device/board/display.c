#include <stddef.h>

#include "board.h"
#include "cru.h"
#include "device/backlight.h"
#include "device/panel.h"
#include "gpio.h"
#include "hal/audio.h"
#include "hal/clock.h"
#include "hal/display.h"
#include "hal/system.h"
#include "registers.h"

#define PANEL_DIRECTION_BITS 0x03FF9000u
#define PANEL_READ_STROBE (1u << 12)
#define PWM_COUNTER (PWM_BASE + 0x0u)
#define PWM_PERIOD (PWM_BASE + 0x4u)
#define PWM_DUTY (PWM_BASE + 0x8u)
#define PWM_CONTROL (PWM_BASE + 0xCu)
#define PWM_CONTROL_RUN 0x00010003u

enum {
    PANEL_READ_STROBE_PIN = 12,
    PANEL_CONTROL_PIN = 15,
    PANEL_ID_BYTES = 4,
    PANEL_FIRST_BUS_PIN = 16,
    PANEL_LAST_BUS_PIN = 25,
    RESET_HIGH_MS = 50,
    RESET_LOW_MS = 150,
    RESET_SETTLE_MS = 30,
    RESET_READY_MS = 120,
    CLEAR_ROWS = 2,
    PWM_GATE = 0x90,
    PWM_RESET = 0x17,
    PWM_RESET_MS = 1,
    PANEL_SPIN_PER_MHZ = 30000
};

static const GpioPin BACKLIGHT = {2, 4};
static const GpioPin PANEL_RESET = {2, 15};

static uint32_t port_shadow;
static PanelRotation rotation;
static uint8_t brightness;
static bool backlight_on;
static bool pwm_running;
static uint32_t pwm_period;
static bool ramping;
static uint32_t ramp_started_ms;
static bool panel_asleep;
static uint32_t panel_changed_ms;

static bool vop_on;
static void vop_command_write(uint8_t command);
static void vop_data_write(uint8_t value);
static void vop_cpu_pixels(const uint16_t *pixels, uint32_t count);

static void write_port(uint32_t word)
{
    volatile uint32_t *port = register_at(GPIO0_BASE + GPIO_DATA);
    *port = word;
    *port = word;
    *port = word | PANEL_BUS_WR;
}

static void send_byte(uint8_t value, bool data)
{
    write_port(panel_bus_word(port_shadow, value, data));
}

static void bus_command(void *context, uint8_t command)
{
    (void)context;
    if (vop_on) {
        vop_command_write(command);
        return;
    }
    send_byte(command, false);
}

static void bus_data(void *context, uint8_t value)
{
    (void)context;
    if (vop_on) {
        vop_data_write(value);
        return;
    }
    send_byte(value, true);
}

static void bus_pixels(void *context, const uint16_t *pixels, uint32_t count)
{
    (void)context;
    vop_cpu_pixels(pixels, count);
}

static void bus_delay(void *context, uint32_t milliseconds)
{
    (void)context;
    board_delay_ms(milliseconds);
}

static const PanelBus BUS = {bus_command, bus_data, bus_pixels, bus_delay, NULL};

static void mux_panel_pins(void)
{
    for (uint8_t pin = PANEL_FIRST_BUS_PIN; pin <= PANEL_LAST_BUS_PIN; pin++) {
        gpio_mux((GpioPin){0, pin}, GPIO_FUNCTION_GPIO);
    }
    gpio_mux((GpioPin){0, PANEL_CONTROL_PIN}, GPIO_FUNCTION_GPIO);
    gpio_mux((GpioPin){0, PANEL_READ_STROBE_PIN}, GPIO_FUNCTION_GPIO);
    gpio_mux(PANEL_RESET, GPIO_FUNCTION_GPIO);
}

static void set_bus_idle(void)
{
    uint32_t direction = register_read(GPIO0_BASE + GPIO_DIRECTION);
    uint32_t data = register_read(GPIO0_BASE + GPIO_DATA);
    port_shadow = (data & ~PANEL_DIRECTION_BITS) | PANEL_READ_STROBE;
    register_write(GPIO0_BASE + GPIO_DATA, port_shadow | PANEL_BUS_WR);
    register_write(GPIO0_BASE + GPIO_DIRECTION, direction | PANEL_DIRECTION_BITS);
}

static void pulse_reset(uint32_t settle_ms)
{
    gpio_output(PANEL_RESET, true);
    board_delay_ms(RESET_HIGH_MS);
    gpio_output(PANEL_RESET, false);
    board_delay_ms(RESET_LOW_MS);
    gpio_output(PANEL_RESET, true);
    board_delay_ms(settle_ms);
}

static uint32_t panel_id;

static uint32_t read_panel_id(void)
{
    uint32_t data_bits = 0xFFu << PANEL_BUS_DATA_SHIFT;
    uint32_t id = 0;
    send_byte(PANEL_READ_ID_COMMAND, false);
    register_write(GPIO0_BASE + GPIO_DATA, port_shadow | PANEL_BUS_WR);
    register_write(GPIO0_BASE + GPIO_DIRECTION,
                   register_read(GPIO0_BASE + GPIO_DIRECTION) & ~data_bits);
    for (int index = 0; index < PANEL_ID_BYTES; index++) {
        register_write(GPIO0_BASE + GPIO_DATA, (port_shadow & ~PANEL_READ_STROBE) | PANEL_BUS_WR);
        board_delay_ms(1);
        id = id << 8 |
             (register_read(GPIO0_BASE + GPIO_INPUT_LEVEL) >> PANEL_BUS_DATA_SHIFT & 0xFFu);
        register_write(GPIO0_BASE + GPIO_DATA, port_shadow | PANEL_BUS_WR);
    }
    register_write(GPIO0_BASE + GPIO_DIRECTION,
                   register_read(GPIO0_BASE + GPIO_DIRECTION) | data_bits);
    return id;
}

void board_display_clear(void)
{
    static uint16_t black[HAL_DISPLAY_WIDTH * CLEAR_ROWS] __attribute__((aligned(4)));
    for (int y = 0; y < HAL_DISPLAY_HEIGHT; y += CLEAR_ROWS) {
        hal_display_write(0, y, HAL_DISPLAY_WIDTH, CLEAR_ROWS, black);
    }
}

static void stop_pwm(void)
{
    if (!pwm_running) {
        return;
    }
    register_write(PWM_CONTROL, 0);
    cru_gate(PWM_GATE, false);
    pwm_running = false;
}

static void drive_backlight(bool level)
{
    stop_pwm();
    gpio_mux(BACKLIGHT, GPIO_FUNCTION_GPIO);
    gpio_output(BACKLIGHT, level);
}

static void start_pwm(void)
{
    cru_gate(PWM_GATE, true);
    cru_reset(PWM_RESET, true);
    board_delay_ms(PWM_RESET_MS);
    cru_reset(PWM_RESET, false);
    pwm_running = true;
}

static void run_pwm(BacklightPwm pwm)
{
    if (!pwm_running) {
        start_pwm();
    }
    register_write(PWM_CONTROL, 0);
    register_write(PWM_DUTY, pwm.duty);
    register_write(PWM_COUNTER, 0);
    register_write(PWM_PERIOD, pwm.period);
    register_write(PWM_CONTROL, PWM_CONTROL_RUN);
    pwm_period = pwm.period;
    gpio_output(BACKLIGHT, true);
    gpio_mux(BACKLIGHT, GPIO_FUNCTION_ALTERNATE);
}

static void apply_backlight(void)
{
    if (!backlight_on) {
        drive_backlight(false);
        return;
    }
    BacklightPwm pwm = backlight_pwm(board_pclk_hz(), backlight_percent(brightness));
    if (pwm.full) {
        drive_backlight(true);
    } else {
        run_pwm(pwm);
    }
}

bool board_display_lit(void)
{
    return backlight_on;
}

bool board_display_awake(void)
{
    return !panel_asleep;
}

static void settle_panel(void)
{
    while (hal_clock_milliseconds() - panel_changed_ms < PANEL_SLEEP_SETTLE_MS) {
        hal_audio_service();
    }
}

static void sleep_panel(void)
{
    if (panel_asleep || !board_saving(HAL_SAVING_PANEL_SLEEP)) {
        return;
    }
    settle_panel();
    panel_sleep(&BUS);
    panel_asleep = true;
    panel_changed_ms = hal_clock_milliseconds();
}

static void wake_panel(void)
{
    if (!panel_asleep) {
        return;
    }
    settle_panel();
    panel_wake(&BUS);
    panel_changed_ms = hal_clock_milliseconds();
    settle_panel();
    panel_show(&BUS);
    panel_asleep = false;
}

void board_display_wake_now(void)
{
    if (panel_asleep) {
        panel_wake(&BUS);
        for (volatile uint32_t spin = 0; spin < hal_system_clock_mhz() * PANEL_SPIN_PER_MHZ;
             spin = spin + 1) {
        }
        panel_show(&BUS);
        panel_asleep = false;
    }
}

void board_backlight_full(void)
{
    ramping = false;
    backlight_on = true;
    drive_backlight(true);
}

static void start_fade_in(void)
{
    ramping = true;
    ramp_started_ms = hal_clock_milliseconds();
    run_pwm(backlight_pwm(board_pclk_hz(), backlight_ramp_percent(0, 0)));
}

void board_display_service(void)
{
    if (!ramping) {
        return;
    }
    uint32_t elapsed_ms = hal_clock_milliseconds() - ramp_started_ms;
    if (elapsed_ms >= BACKLIGHT_RAMP_MS || !pwm_running) {
        ramping = false;
        apply_backlight();
        return;
    }
    uint32_t percent = backlight_ramp_percent(backlight_percent(brightness), elapsed_ms);
    register_write(PWM_DUTY, backlight_duty(pwm_period, percent));
}

void board_backlight_off(void)
{
    ramping = false;
    backlight_on = false;
    drive_backlight(false);
}

static void vop_clock_changed(void);
static void vop_bring_up(void);

void board_display_after_clock_change(void)
{
    vop_clock_changed();
    if (pwm_running) {
        apply_backlight();
    }
}

void board_display_start(void)
{
    brightness = HAL_DISPLAY_BRIGHTNESS_MAX;
    backlight_on = false;
    drive_backlight(false);
    mux_panel_pins();
    gpio_output(PANEL_RESET, true);
    set_bus_idle();
    pulse_reset(RESET_SETTLE_MS);
    pulse_reset(RESET_READY_MS);
    panel_id = read_panel_id();
    panel_send_init(&BUS, panel_variant_of_id(panel_id));
    vop_bring_up();
    board_display_clear();
}

#define VOP_BASE 0x60070000u
#define VOP_CONTROL (VOP_BASE + 0x00u)
#define VOP_TIMING (VOP_BASE + 0x08u)
#define VOP_SIZE (VOP_BASE + 0x0Cu)
#define VOP_WATERMARK (VOP_BASE + 0x10u)
#define VOP_INT_STATUS (VOP_BASE + 0x20u)
#define VOP_COMMAND (VOP_BASE + 0x28u)
#define VOP_DATA (VOP_BASE + 0x2Cu)
#define VOP_START (VOP_BASE + 0x30u)
#define PANEL_DMA_BASE 0x60000000u
#define PANEL_DMA_SAR (PANEL_DMA_BASE + 0x00u)
#define PANEL_DMA_DAR (PANEL_DMA_BASE + 0x08u)
#define PANEL_DMA_LLP (PANEL_DMA_BASE + 0x10u)
#define PANEL_DMA_CTL_LOW (PANEL_DMA_BASE + 0x18u)
#define PANEL_DMA_CTL_HIGH (PANEL_DMA_BASE + 0x1Cu)
#define PANEL_DMA_CFG_LOW (PANEL_DMA_BASE + 0x40u)
#define PANEL_DMA_CFG_HIGH (PANEL_DMA_BASE + 0x44u)
#define PANEL_DMA_CONFIG (PANEL_DMA_BASE + 0x398u)
#define PANEL_DMA_CHANNEL_ENABLE (PANEL_DMA_BASE + 0x3A0u)

enum {
    VOP_GATE = 127,
    VOP_RESET = 13,
    VOP_COMMANDS = 0x800,
    VOP_PIXELS = 0x002,
    VOP_PACKED_PIXELS = (1u << 4) | (3u << 1),
    VOP_WATERMARKS = 8u << 8 | 8u,
    VOP_TIMING_HZ_PER_STEP = 8000000,
    VOP_RAW_FIFO_EMPTY = 1u << 4,
    VOP_DRAIN_SPINS = 100000,
    VOP_SETTLE_US = 5,
    PANEL_DMA_CHANNEL = 1u << 0,
    PANEL_DMA_ENABLE = PANEL_DMA_CHANNEL << 8 | PANEL_DMA_CHANNEL,
    PANEL_DMA_DISABLE = PANEL_DMA_CHANNEL << 8,
    PANEL_DMA_MAX_WORDS = 4000,
    PANEL_DMA_CFG_LOW_VOP = 0x800,
    PANEL_DMA_CFG_HIGH_VOP = 0x7 | 14u << 11,
    PANEL_DMA_CTL_WORDS = 0x100 | 1u << 20 | 2u << 1 | 2u << 4 | 1u << 11 | 1u << 14,
    PANEL_DMA_SUSPEND = 0x100,
    PANEL_DMA_FIFO_EMPTY = 0x200,
    PANEL_DMA_SPINS = 2000000
};

static bool vop_pixels_on;
static bool vop_dma_pending;

static uint32_t vop_timing(void)
{
    uint32_t steps = hal_system_clock_mhz() * 1000000u / VOP_TIMING_HZ_PER_STEP;
    uint32_t pulse = ((2u * steps) / 3u) >> 2;
    uint32_t setup = steps >> 3;
    uint32_t hold = setup;
    pulse = pulse < 1 ? 1 : pulse > 63 ? 63 : pulse;
    hold = hold < 1 ? 1 : hold > 31 ? 31 : hold;
    return setup << 12 | pulse << 5 | hold;
}

static void mux_panel_bus(uint32_t function)
{
    for (uint8_t pin = PANEL_FIRST_BUS_PIN; pin <= PANEL_LAST_BUS_PIN; pin++) {
        gpio_mux((GpioPin){0, pin}, function);
    }
    gpio_mux((GpioPin){0, PANEL_CONTROL_PIN}, function);
}

static void dma_stop(void)
{
    register_write(PANEL_DMA_CFG_LOW, PANEL_DMA_CFG_LOW_VOP | PANEL_DMA_SUSPEND);
    for (int poll = 0;
         poll < PANEL_DMA_SPINS && (register_read(PANEL_DMA_CFG_LOW) & PANEL_DMA_FIFO_EMPTY) == 0;
         poll++) {
    }
    register_write(PANEL_DMA_CHANNEL_ENABLE, PANEL_DMA_DISABLE);
    for (int poll = 0; poll < PANEL_DMA_SPINS &&
                       (register_read(PANEL_DMA_CHANNEL_ENABLE) & PANEL_DMA_CHANNEL) != 0;
         poll++) {
    }
    register_write(PANEL_DMA_CFG_LOW, PANEL_DMA_CFG_LOW_VOP);
}

static void vop_reset(void)
{
    cru_reset(VOP_RESET, true);
    board_delay_us(10);
    cru_reset(VOP_RESET, false);
    cru_gate(VOP_GATE, true);
    register_write(VOP_TIMING, vop_timing());
    register_write(VOP_CONTROL, VOP_COMMANDS);
    vop_pixels_on = false;
}

static void vop_wait_dma(void)
{
    if (!vop_dma_pending) {
        return;
    }
    vop_dma_pending = false;
    for (int spin = 0; (register_read(PANEL_DMA_CHANNEL_ENABLE) & PANEL_DMA_CHANNEL) != 0; spin++) {
        if (spin == PANEL_DMA_SPINS) {
            dma_stop();
            vop_reset();
            return;
        }
    }
}

static void vop_settle(void)
{
    for (int spin = 0;
         spin < VOP_DRAIN_SPINS && (register_read(VOP_INT_STATUS) & VOP_RAW_FIFO_EMPTY) == 0;
         spin++) {
    }
    board_delay_us(VOP_SETTLE_US);
}

static void vop_commands_on(void)
{
    vop_wait_dma();
    if (vop_pixels_on) {
        vop_settle();
        register_write(VOP_CONTROL, VOP_COMMANDS);
        vop_pixels_on = false;
    }
}

static void vop_command_write(uint8_t command)
{
    vop_commands_on();
    register_write(VOP_COMMAND, command);
}

static void vop_data_write(uint8_t value)
{
    vop_commands_on();
    register_write(VOP_DATA, value);
}

static void vop_cpu_pixels(const uint16_t *pixels, uint32_t count)
{
    vop_wait_dma();
    register_write(VOP_CONTROL, VOP_PIXELS);
    vop_pixels_on = true;
    volatile uint32_t *data = register_at(VOP_DATA);
    for (uint32_t index = 0; index < count; index++) {
        *data = pixels[index];
    }
}

static void dma_start(const void *source, uint32_t words)
{
    register_write(PANEL_DMA_SAR, (uint32_t)(uintptr_t)source);
    register_write(PANEL_DMA_DAR, VOP_DATA);
    register_write(PANEL_DMA_LLP, 0);
    register_write(PANEL_DMA_CTL_LOW, PANEL_DMA_CTL_WORDS);
    register_write(PANEL_DMA_CTL_HIGH, words);
    register_write(PANEL_DMA_CFG_LOW, PANEL_DMA_CFG_LOW_VOP);
    register_write(PANEL_DMA_CFG_HIGH, PANEL_DMA_CFG_HIGH_VOP);
    register_write(PANEL_DMA_CHANNEL_ENABLE, PANEL_DMA_ENABLE);
    vop_dma_pending = true;
}

static bool vop_dma_write(int width, int height, const uint16_t *pixels)
{
    vop_wait_dma();
    uint32_t count = (uint32_t)(width * height);
    if ((count & 1u) != 0 || ((uintptr_t)pixels & 3u) != 0 ||
        (register_read(PANEL_DMA_CHANNEL_ENABLE) & PANEL_DMA_CHANNEL) != 0) {
        return false;
    }
    vop_commands_on();
    register_write(VOP_CONTROL, VOP_PACKED_PIXELS);
    register_write(VOP_WATERMARK, VOP_WATERMARKS);
    register_write(VOP_SIZE, (uint32_t)(height - 1) << 12 | (uint32_t)(width - 1));
    register_write(VOP_START, 1);
    register_write(PANEL_DMA_CONFIG, 1);
    vop_pixels_on = true;
    const uint32_t *words = (const uint32_t *)(const void *)pixels;
    uint32_t left = count / 2u;
    while (left > 0) {
        uint32_t block = left < PANEL_DMA_MAX_WORDS ? left : PANEL_DMA_MAX_WORDS;
        vop_wait_dma();
        dma_start(words, block);
        words += block;
        left -= block;
    }
    return true;
}

static void vop_bring_up(void)
{
    vop_reset();
    register_write(GPIO0_BASE + GPIO_DATA, port_shadow | PANEL_BUS_WR);
    mux_panel_bus(GPIO_FUNCTION_ALTERNATE);
    vop_on = true;
}

static void vop_clock_changed(void)
{
    if (vop_on) {
        vop_commands_on();
        register_write(VOP_TIMING, vop_timing());
    }
}

void hal_display_wait(void)
{
    vop_wait_dma();
}

uint32_t hal_display_panel_id(void)
{
    return panel_id;
}

void hal_display_set_rotation(HalDisplayRotation turned)
{
    rotation = (PanelRotation)turned;
    panel_set_rotation(&BUS, rotation);
}

static bool vop_dma_write(int width, int height, const uint16_t *pixels);

void hal_display_write(int x, int y, int width, int height, const uint16_t *pixels)
{
    wake_panel();
    panel_begin_write(&BUS, (PanelWindow){x, y, width, height, rotation});
    if (!vop_dma_write(width, height, pixels)) {
        vop_cpu_pixels(pixels, (uint32_t)(width * height));
    }
}

void hal_display_present(void)
{
}

void hal_display_set_power(bool on)
{
    board_start_record_step(on ? HAL_START_STEP_BACKLIGHT_ON : HAL_START_STEP_BACKLIGHT_OFF);
    bool was_on = backlight_on;
    backlight_on = on;
    ramping = false;
    if (on) {
        wake_panel();
    }
    if (on && !was_on) {
        start_fade_in();
    } else {
        apply_backlight();
    }
    if (!on) {
        sleep_panel();
    }
    board_start_record_step_done();
}

void hal_display_set_brightness(uint8_t level)
{
    brightness = level;
    if (backlight_on && !ramping) {
        apply_backlight();
    }
}

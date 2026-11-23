# Hardware

A map of the Echo Mini hardware Rippl drives, for reading `platform/device`. Most of it was first worked out by the [ReChord](https://github.com/brnomt/ReChord) and EchoPod projects; the display controller and its DMA path were worked out from FiiO's firmware and Rockchip's RKNanoD reference manual (TRM).

## Overview

| Part | Detail |
|---|---|
| SoC | Rockchip RKNanoD, two Cortex-M3 cores, 24 MHz at boot, 96 or 200 MHz under Rippl |
| Display | 320 × 170 RGB565 panel with its own memory, on an 8-bit 8080 bus |
| Storage | 8 GB eMMC and a microSD slot, both on DW-MMC controllers |
| Audio | Two Cirrus Logic CS43131 DACs, 3.5 mm and 4.4 mm outputs |
| USB | DesignWare USB 2.0 OTG, high speed |
| Other | Bluetooth module on UART1, clock chip on I2C1, keys on the ADC |

## Processor and memory

- Core 0 runs Rippl. Core 1 runs one decoder program at a time (FLAC, ALAC and WAV; MP3; AAC; or Opus), loaded on demand, with an LPC accelerator beside it for FLAC.

| Range | Size | Use |
|---|---|---|
| `0x03004000`–`0x0304C000` | 288 KB | Core 0 data RAM (cleared by the boot ROM) |
| `0x0304C000`–`0x03050000` | 16 KB | Free once Rippl runs: the language pack. FiiO's loader runs from `0x0304A000`–`0x0304D9DC` while it loads Rippl, so nothing loaded or cleared at boot may reach `0x0304A000` |
| `0x03050000`–`0x0307A000` | 168 KB | Core 0 code |
| `0x0307A000`–`0x0308AA00` | 66.5 KB | High RAM scratch: the Cover Flow shuffle mark, the theme's head and wallpaper; overlays (rescan, USB, EQ fit) load over it |
| `0x0308AA00`–`0x0308B400` | 2.5 KB | The theme engine: boxes, the wallpaper copy and icon masks |
| `0x0308B400`–`0x0308F000` | 15 KB | Start-up code while starting, then the Now Playing faces (the scene interpreter) and the worn theme's scenes |
| `0x0308F000`–`0x0309EFFF` | 64 KB | Glyph cache, resident tables |
| `0x01000000`–`0x0105FFFF` | 384 KB | Core 1 RAM; rescan scratch while playback is stopped |

## Display

**Bus.** D0–D7 on GPIO0.16–23, WR on GPIO0.24, D/C on GPIO0.25, chip select on GPIO0.15, read strobe on GPIO0.12, reset on GPIO2.15. Two panel variants exist, told apart by the ID the panel returns to command `0x04`, each with its own init sequence; the visible area starts at panel row 35. The backlight is a 10 kHz PWM on GPIO2.4.

**GPIO path.** Rippl bit-bangs the bus only at start-up, for the panel ID read and init sequence: two port writes per byte (data with WR low, then WR high), four per pixel. One write, about 40 ns at 200 MHz, already exceeds the panel's write-low time.

**LCD controller (VOP).** The same pins are the chip's LCD interface (function 1 on GPIO0_C0–C7, D0, D1 and B7), driven by VOP at `0x60070000` (TRM chapter 15, LCDC in the memory map). FiiO's firmware drives the panel this way, and Rippl switches to it after the ID read and init sequence:

- **Bring-up:** release CRU reset 13, open clock gate 127, set `TIMING` from the CPU clock as FiiO does. With `x` = clock / 8 MHz: setup `x >> 3`, pulse `((2x)/3) >> 2` (1–63), hold `x >> 3` (1–31); `0x3083` at 200 MHz.
- **Commands:** `CON` = `0x800`, then the command through `CMD` (+0x28) and parameters through `DATA` (+0x2C).
- **Pixels from the CPU:** `CON` = `0x002`, one 16-bit colour per `DATA` store. Rippl uses this only for a window DMA can't take (an odd pixel count or a buffer that isn't word-aligned). Don't write `LCD_SIZE` in this mode: VOP then counts the writes as frame pixels and sends nothing.
- **Between bursts:** before writing `CON` or a command after pixels, wait for the FIFO to empty (raw interrupt status bit 4) and about 5 µs more, or the next window is lost.

**DMA.** Pixels go to VOP on DMA channel 0, request 14 (audio uses channel 5): two pixels per 32-bit word with `CON` = `0x016` (half-word swap, four-phase split), bursts of 4, both FIFO watermarks at 8, and `LCD_SIZE` and `START` set for each window. Without `LCD_SIZE` and `START`, VOP stops requesting data partway into the window.

| Path | Full 320 × 170 frame |
|---|---|
| GPIO | 8.7 ms |
| VOP, CPU-fed | 7.6 ms |
| VOP, DMA | 7.4 ms, with the CPU free |

Timings faster than 2/3/2 cycles (setup/pulse/hold) at 200 MHz corrupt pixels whether the CPU or DMA feeds them. Rippl keeps FiiO's timing.

## Keys

▶‖, ≡, ↩ and the volume rocker sit on a resistor ladder read through the ADC. Power is a separate input.

## Storage

- The eMMC holds, from the start: an ID block, firmware slot 1, firmware slot 2, a SAVE block, then the FAT32 user volume (ECHO MINI). Rippl never writes the ID block, the SAVE block or the gap between them.
- Each slot is half the firmware area (34 MiB on the unit tested); Rippl's images use 5 or 8 MiB of it. Slot 1 starts at rockusb sector 0, slot 2 at sector 69632.
- The microSD slot has card detect. Rippl reads and writes FAT16, FAT32 and exFAT.

## Audio

- Two CS43131 DACs, set up over I2C0 and fed by I2S1 through DMA from a ring in data RAM.
- GPIO0.13 high means a 3.5 mm plug; GPIO2.8 low means 4.4 mm.

## Clock

The player has a PCF8563-style clock chip (I2C address `0xA2`) on I2C1 at `0x40050000`: CRU gate `0x84`, soft reset `0x1B`, SDA on GPIO2.12 and SCL on GPIO2.13 with function 3, and GRF `+0xBC` = `0x000C0008` for pin group C, at 100 kHz. Registers follow the PCF8563 map: seconds at `0x02` with bit 7 VL (power was lost), up to the year at `0x08`. Rippl reads it at start, every 10 minutes and after each USB session, and writes it when Tide leaves a clock record or the time is set by hand. It keeps standard local time. Whether every unit's chip keeps time across power-off isn't confirmed.

## USB

- **Storage:** both drives as one mass-storage device, high speed.
- **DAC:** USB Audio Class 1.0, full speed, 16 or 24-bit at 44.1 or 48 kHz, with a HID consumer-control interface for media keys.
- **Hi-Res DAC:** USB Audio Class 2.0, high speed, 44.1–384 kHz at 16, 24 or 32 bits (up to 96 kHz at full speed), with an explicit feedback endpoint and its own product ID.
- Audio packets are taken in the controller's interrupt (IRQ 5), one per microframe in Hi-Res DAC mode, straight into the output ring. The FIFO RAM is small: at reset, RX 147 words and TX FIFO 0 128 words.
- With the buttons held and USB connected at power-up, FiiO's loader starts its recovery mode, `2207:262d` with `bcdUSB` 2.01 ([recovery](../development/recovery.md)). The boot ROM's own maskrom (`bcdUSB` 2.00) only starts when no storage holds a bootable boot block.

## Bluetooth

An external module with its own stack, powered from GPIO2.7 and driven over UART1 at 115,200 baud with short ASCII commands.

- Audio reaches the module only with the stock start-up settings applied when routing to it: GRF `+0xC4` = `0x00010000`, CRU `+0x84` = `0x00920092` and `+0xA0` = `0x00020002` (gates `0x11`, `0x14`, `0x17`, `0x81` off), CRU `+0xC0` = `0x00680068` (I2S0, SPI0 and SPI1 held in reset), GPIO2.9 low, stock's pulls on GPIO0.13 and GPIO2.8, and `CY0 00` before `CY0 1E` on connect. Which of these are needed isn't narrowed down.
- Only GPIO2.0 high mutes the wired output. Switching GPIO2.0/2.14 disturbs the jack-detect pins, so `board/jack.c` ignores them while audio goes to Bluetooth and for 500 ms after each change.

## Battery

Read through the ADC. Rippl shuts down below 3450 mV, as FiiO's firmware does.

## Firmware image

- An image is an 8 MiB `RKnanoFW` container: an outer header, a table of load rows, code and data, a copy of the first 512 bytes at the firmware size (`0x74C000`), and a CRC-32 trailer at `0x800000`. `tools/make_image.py` builds it. The installer still accepts older 5 MiB images.
- After the code and data, each part the player loads on demand has a slot with room to grow, then the glyph data fills the rest:

| Offset | Slot | Holds |
|---|---|---|
| `0x000000` | 192 KB | Header, load rows, code and data |
| `0x030000` | 16 KB | Start-up code (`ESTP`) |
| `0x034000` | 16 KB | Now Playing faces (`ENPF`) |
| `0x038000` | 20 KB | Resident tables (`ERES`) |
| `0x041000` | 96 KB | Rescan overlay (`EOVL`) |
| `0x059000` | 72 KB | USB overlay (`EUSB`) |
| `0x06B000` | 40 KB | EQ fit overlay (`EEQF`) |
| `0x075000` | 5 × 132 KB | Core 1 programs: MP3, FLAC, AAC, Opus, EQ fit (`EC1M`) |
| `0x11A000` | 8 KB | Theme engine (`EENG`) |
| `0x11C000` | 512 B | Glyph table (`EGPT`) |
| `0x11C200`–`0x74C000` | | Glyph faces, language packs and built-in themes |

- Every loaded part starts with a 16-byte header: its magic, size, a CRC-32 of its body and the CRC-32 of the running code, so it only loads with the firmware it was built with.
- **Start-up code.** What runs only while the player starts (settings, the player and stores, building the screens, reading a theme) is linked into `.startup` at the faces' address, loaded from its slot, run and released. Theme loading borrows it again when a theme is picked or its wallpaper must be read back. If it can't be loaded the player says so and stays on while USB power is present, so a new image can still be copied.
- **Theme engine.** Box, wallpaper and icon drawing load into the 2.5 KB area at the top of High RAM. A rescan, USB or EQ fit overlay wipes it; until it is loaded again boxes are flat, the wallpaper is the plain background and icons are the built-in shapes.
- FiiO's firmware installs the highest-numbered `HIFIECnn.IMG` at the top of internal storage. FiiO's own images are 32 MiB.

# Testing on a player

Installing your own builds, checking them, and what to do when one fails. Make sure you can [recover the player](recovery.md) before you start.

## First install

Follow [install](../user/install.md). FiiO's updater hasn't been tried with the 8 MiB image.

### Direct maskrom write

If FiiO's updater won't take the file, maskrom can write the image straight into both firmware slots. Each slot holds the image's first `0x800000` bytes. On the unit tested, slot 1 is at rockusb sector 0 and slot 2 at sector 69632 (`0x11000`); check yours first: `rl 0 1` and `rl 69632 1` must both show `RKnanoFW` at `0x1F8`.

```sh
cmake --preset device && cmake --build --preset device
head -c 8388608 build/device/RIPPL.IMG > build/device/slot.bin
cd external/rkdeveloptool
./rkdeveloptool ld && ./rkdeveloptool td
./rkdeveloptool wl 69632 ../../build/device/slot.bin
./rkdeveloptool wl 0 ../../build/device/slot.bin
./rkdeveloptool rl 0 1 s1.bin && ./rkdeveloptool rl 69632 1 s2.bin
./rkdeveloptool rd
```

Enter maskrom first ([recovery](recovery.md#2-enter-maskrom)). Both read-backs must equal the first 512 bytes of `slot.bin`.

## Updates

1. Connect the player. It shows "Computer found" and two drives appear.
2. Copy the image to the top level of internal storage and compare hashes:
   - Windows: `Copy-Item build\device\RIPPL.IMG E:\` and `Get-FileHash E:\RIPPL.IMG`.
   - macOS: `cp -X build/device/RIPPL.IMG "/Volumes/ECHO MINI/"`, remove the `._RIPPL.IMG` it leaves, and `shasum -a 256` both copies.
3. Eject both drives. The player shows "Checking RIPPL.IMG", "Installing update" with a percentage, then "Update installed".
4. Unplug. The player restarts into the new build about half a second later.

Before writing, the updater checks the image's shape, load rows, entry vector, header copy and CRC. It writes slot 2, then slot 1, reading every chunk back. It also accepts FiiO's own 8 GB image and refuses the 512 MB model's.

## After an install

| Check | Expect |
|---|---|
| Home | Music, Now Playing and Settings, and Diagnostics once it's turned on |
| Diagnostics → System → Firmware | The commit you built (a `+` means uncommitted changes) |
| Diagnostics → Key test | Every key registers |
| Power | A tap toggles the screen; holding 2 s switches off (with a cable, the charge screen) |
| USB | "Computer found", and the drives mount within about 10 s |
| Card | `/.rippl` with its save files, then the library in `/.rippl/library` and the covers in `/.rippl/covers`, committed with `/.rippl/manifest.bin` |

## Diagnostics

Turn it on in Settings → System → Diagnostics, or put an empty file named `debug` at the top of the internal drive. Its rows are grouped:

| Group | Rows |
|---|---|
| Audio | Audio output (which core sends the I2S words), Jack (single-ended), Jack (balanced), Bluetooth, Fit log (write each equaliser fit's times to `/.rippl/equaliser/FIT.LOG`, not saved) |
| Display | Panel |
| Storage | SD clock, SD transfers |
| USB | Last USB, USB errors |
| Power | Battery, Last battery, Power, Power taps, Savings (turn single power savings off, such as the 96 MHz screen-off clock) |
| System | Firmware, CPU clock (24, 96 and 200 MHz), Core 1 and LPC accel self-tests, Clock, Key test, Performance (the overlay below) |
| Boot | Boot hold, Last start, Last step, Last stack, Stack, Last update |

### Performance overlay

Green monospaced text at the top left, over the status bar, on a darkened strip. Two lines in landscape and four in portrait: frame times and draw parts, then the audio buffer and loads, then per-block times. Each field is a letter and a zero-padded number of fixed width, so nothing moves as values change; times are in milliseconds.

```
F016 u02 d009 s004 cv09 r03 t02 bg01 h02 a00
b380 x000 o000 L12%/45% dec01/003 rd00/002
```

| Field | Meaning |
|---|---|
| `F` `u` `d` `s` | Frame time, then update, draw and send |
| `cv` `r` `t` `bg` `h` `a` | Draw parts: covers, reflections, text, backdrop, frame hash, audio service |
| `b` | Audio buffered (ms), at most 437 |
| `x` `o` | Underruns and output underruns. They count past 999 by starting again at 000, and the letter turns upper case (`X`, `O`) |
| `L` | Core 0 audio load / core 1 load, `00`–`99` %, `xx` at 100 % or more |
| `dec` `rd` | Per audio block, average/longest: decode and card read |

A time too long for its digits shows as all nines.

### Core 1 load

The core 1 load, the second number after `L`, is a share of core 1's current clock. Core 1 runs at 200 MHz for AAC and Opus, for FLAC and ALAC above 96 kHz or above 24 bits, for rates it raises (8–24 kHz) and while the equaliser is on; otherwise at 100 MHz.

Measured on 60 s of stereo white noise encoded with 32nd-order prediction, the heaviest FLAC decoding:

| Track | Equaliser | Core 1 load | Core 1 clock |
|---|---|---|---|
| 96 kHz 24-bit | Off | 16 % | 100 MHz |
| 96 kHz 24-bit | 10 filters | 82 % | 200 MHz |
| 192 kHz 24-bit | Off | 33 % | 200 MHz |

The equaliser is off above 96 kHz: at 192 kHz a 10-filter preset didn't keep up.

## When a build fails

| Symptom | Next step |
|---|---|
| Black screen, drives appear over USB | Copy a fixed image with the updater |
| Black screen, nothing over USB | [Maskrom recovery](recovery.md), then a direct maskrom write |
| "Update not installed" | The file isn't a valid image for this player; rebuild and copy again |
| "Update failed" | Copy again and retry before switching off |
| A STALL or FAULT screen | Photograph it, then press RST |

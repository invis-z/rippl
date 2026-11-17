# USB

Plugged into a computer, the player is either a pair of drives (Storage) or a sound card (DAC or Hi-Res DAC). Settings → System → **USB mode** picks the default; with **Ask when connected** on (the default), plugging in asks *Storage*, *DAC*, *Hi-Res DAC* or *Charge only* and takes the saved mode after 5 seconds. A charger never asks.

## Storage

1. Connect the player. Two drives appear: internal storage and the SD card.
2. Copy music to either.
3. **Eject both drives before you unplug.** Unplugging first can lose the last changes, and the player warns you.
4. If music changed, the player rescans by itself. Copying other files doesn't trigger a rescan.

During a long copy the screen goes dark after 30 seconds; any button lights it again. If the drives drop off while the cable stays in, the player reconnects by itself.

macOS leaves `._` files beside what it copies; Rippl ignores them, and `dot_clean` removes them.

### USB errors

Diagnostics → USB → **USB errors** shows what went wrong in the last session, or *None*:

| Code | Meaning |
|---|---|
| `E1/2` | Commands that failed on internal storage / the SD card |
| `r3` | SD card transfers that failed and were retried |
| `i1` | Times the card had to be started again |
| `s4:A208` | The card controller's step and status at the last failure |
| `b1` | Times the computer reset a working connection |
| `v2` | Moments the cable's power reading dropped briefly |
| `FS` / `fs` | The link fell from high speed to full speed this session / an earlier one |

## DAC and Hi-Res DAC

The player becomes a sound card: the computer's or phone's audio plays through the player's headphone outputs. No driver is needed on Windows 10 and later, macOS, Linux, Android or iOS (phones need an adapter that passes data).

| Mode | Formats | USB |
|---|---|---|
| DAC | 16 or 24-bit, 44.1 or 48 kHz | Audio Class 1.0, full speed |
| Hi-Res DAC | 16, 24 or 32-bit, 44.1–384 kHz | Audio Class 2.0, high speed (up to 96 kHz on a full-speed port); Windows 10 1703 or later |

1. Set USB mode to DAC or Hi-Res DAC (or pick it when asked), and plug in. The player shows the sample rate and bit depth.
2. On the computer, choose Rippl as the sound output.
3. Set the volume on the computer, the player, or both. DAC modes have their own remembered volume, starting at 60 of 85. Start low.
4. The player's buttons act like a headset's: ▶ play/pause, holding vol± next/previous, ≡ next when Menu button is set to Next track.
5. Unplug when done, or hold ↩ for a second to go back to the player's own music.

The volume is applied in software while in DAC mode. Music on the player stops and the drives don't appear: switch back to Storage to copy music.

Hi-Res DAC keeps the player's clock in step with the computer's through a feedback endpoint. Plain DAC mode occasionally drops or repeats a sample instead, up to a few hundred a minute in short bursts, which is normal.

Hold ≡ to show the stream counters; hold again to hide them. *missed* (packets that never arrived) and *restarts* should stay at 0 while music plays; if *missed* rises, please report the panel.

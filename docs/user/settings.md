# Settings

Open Settings from Home, or hold ≡ anywhere except on Now Playing. The top of the list holds **Shuffle**, **Repeat**, **Bluetooth**, **Equaliser** and **Sleep timer**; the rest are in five groups. Each row shows its value, and a change applies at once.

## Sound

| Setting | Values |
|---|---|
| Gain | Low, High |
| Output | Auto, 3.5 mm, 4.4 mm. Both jacks share the same DACs, so this picks the volume table. |
| Filter | The DAC's five filters, by name |
| Balance | Left to right |
| DRE | Off, On, On with a lower threshold (the DACs' dynamic range enhancement; changing it restarts the DACs briefly) |

Volume has 85 steps of 1 dB.

## Display

| Setting | Values |
|---|---|
| Theme | Dark (default), Light, Fjord, Dusk, Neon, Phosphor, Paper, Olive, High contrast, then up to eight theme files from `Themes` on the card ([themes](themes.md)) |
| Accent colour | The selection bar's colour, on Dark and Light |
| Highlight | Gradient or flat, on themes with a gradient bar |
| Lyrics | On, Off |
| Brightness, Screen timeout | |
| Orientation | Landscape, Portrait, Portrait flipped |
| Animations | Normal, Slower (1.5×), Faster (0.6×) |
| Battery percentage | Show or hide the number |
| Portrait status bar | Standard, Large |
| Clock | Show the time in the status bar |

Cover Flow keeps its black stage on every theme, with the theme's text colours, or the dark look's on light themes such as Light, Paper, Olive and Tape Window. The cover panel on Home takes the theme's background.

## Library

| Setting | Values |
|---|---|
| Music folders | Which top-level folders are in the library ([library](library.md#music-folders)) |
| Rescan library | Rebuild the library now |
| Cover Flow | 5 or 3 covers |
| Cover Flow order | By album artist or album title |
| Artist albums | List or Cover Flow |
| Folders & playlists | List or Cover Flow |

## Buttons

| Setting | Values |
|---|---|
| Volume buttons | *Tap: volume, hold: skip* (default), *Tap: skip, hold: volume*, *Tap: volume, hold: seek* |
| Swap volume buttons | Turns the volume direction around; lists, skips and seeking stay |
| Menu button | ≡ on Now Playing opens Up Next (default) or skips to the next track |
| Power on | Press, or Hold: start only when Power is held for about a second, so a tap in a pocket doesn't start it |

Right after a volume tap, holding the rocker keeps changing the volume in every mode.

## System

| Setting | Values |
|---|---|
| Language | English and 简体中文 built in; other languages as packs (below) |
| Date & time | Set the clock ([clock](#clock)) |
| Daylight saving | Off, Europe, US & Canada, Australia, New Zealand, From computer |
| Start screen | Home, Now Playing, or a Music list |
| Auto off | Off, 1, 5 or 10 minutes without a key press (never while music plays or on cable power) |
| USB mode | Storage, DAC, Hi-Res DAC ([USB](usb.md)) |
| Ask when connected | Ask each time a computer is plugged in |
| Diagnostics | Off or On: shows Diagnostics on Home ([below](#diagnostics)) |

### Languages

Japanese, Korean, German, French, Spanish, Italian, Portuguese, Dutch, Swedish, Danish, Norwegian, Finnish, Russian, Ukrainian, Polish and Indonesian are packs on the internal drive: `tide languages install de` copies German, or copy `<code>.bin` from the release's `rippl-languages.zip` into a `Languages` folder there. Settings → Language lists what it finds. A change applies after a restart. Song, artist and album names are never translated.

### Clock

The player's clock chip keeps time while it's off. Set it in either of two ways:

- **Tide** writes the computer's time, time zone and daylight saving rule to the card on every sync; the player takes it when the USB session ends ("Clock set from the computer").
- **Date & time:** vol± choose a row, ▶ edits it, ↩ saves. Set the standard (winter) time zone: UTC+01:00 for Berlin even in summer.

**Daylight saving** moves the clock by itself at the right moment. *From computer* follows the rule Tide read from the computer, which covers places the list doesn't name. The chip keeps standard time, so Diagnostics → System → Clock is an hour behind the status bar in summer.

### Diagnostics

Diagnostics is hidden until Settings → System → **Diagnostics** is On. An empty file named `debug` at the top of the internal drive shows it too, whatever the setting; the player looks for the file when it starts and after USB. Its rows are grouped: Audio, Display, Storage, USB, Power, System and Boot.

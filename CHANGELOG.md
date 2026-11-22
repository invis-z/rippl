# Changelog

Notable changes to Rippl. Rippl is a fork of [EchoPod](https://github.com/BasilSkyWalk/echopod) 0.12.0; changes before the fork are in EchoPod's own history.

## [Unreleased]

The first Rippl release.

### Before you update

- **Files move from `/.echo` to `/.rippl`.** The first start sets the card up again: settings, the equaliser, resume, Up Next, favourites, bookmarks, Bluetooth devices, music folder choices and the library start fresh. Old `/.echo` files can be deleted.
- **The firmware is an 8 MiB `RIPPL.IMG`.** It can't be installed from EchoPod, which accepts only 5 MiB images ([install](docs/user/install.md#from-echopod)).
- **Only English and Simplified Chinese are built in.** Install your language's pack first (`tide languages install <code>`), or the player starts in English.

### Added

- 352.8 and 384 kHz WAV, FLAC and ALAC, and 32-bit FLAC and ALAC up to 96 kHz. 32-bit WAV goes to the DAC with all 32 bits.
- **Hi-Res DAC** USB mode: a USB Audio Class 2.0 sound card at 44.1–384 kHz and 16, 24 or 32 bits, with clock feedback to the computer.
- Tape Window, a Now Playing face that turns the screen into the player's window onto a cassette.
- Daylight saving for the clock: Europe, US & Canada, Australia, New Zealand, or the computer's rule through Tide.
- **DRE** setting for the DACs' dynamic range enhancement.
- `/.rippl/errors.log`, listing files the player couldn't open or use, with the reason.
- **Diagnostics** is hidden until Settings → System → Diagnostics is on, or a file named `debug` is on the internal drive, and its rows are grouped by subsystem. There's one firmware image.
- **Tide**, replacing rippl-sync: builds the player's own library files, fits the EQ presets, lists bookmarks and installs language packs. Not yet tested on a real card.

### Changed

- **Library:** stored in segment files that grow with the music, with 32-bit counts. Rescans read only new or changed files; resume, Up Next and covers survive them. Paths up to 1,024 bytes play.
- **Screen:** sent through the LCD controller by DMA while the next part is drawn, so sending a frame costs almost no time.
- **USB DAC:** packets are taken in an interrupt, so a busy screen can't drop audio; the stream fades out when the computer stops; a new screen shows the format and volume, and holding ≡ shows stream counters.
- **Equaliser:** presets come from `.txt` files in `/EQ` (Equalizer APO, AutoEQ), up to 100. Each is fitted to every rate from 8 to 96 kHz after a scan, so it sounds the same at every rate; hold ▶ on a preset to refit it. All formats are equalised on the second processor, which now decodes WAV and FLAC too.
- **Upsampling:** 32 kHz plays natively; 8 and 16 kHz are raised to 32 kHz, and 11–24 kHz to 44.1 or 48 kHz, with a 64-tap filter instead of straight lines.
- **Volume:** 85 steps of exactly 1 dB; Bluetooth gets the 3.5 mm jack's level. Filter names the DAC's five filters.
- **Faces:** fill the whole screen; Turntable shows the cover as its turning label, and Cassette and Turntable progress across the whole album.
- **Settings:** Start screen moves to System, Lyrics to Display.
- **Buttons:** in portrait, taps turn the volume the other way while holds still skip and seek. Swap volume buttons reverses only the volume.
- **Languages:** extra languages load as packs from the internal drive; the remaining English strings are translated; GBK and JIS X 0213 names draw.
- **Memory:** the language pack and part of the code move to RAM free after start, giving room for a bigger glyph cache.
- **Firmware image:** rearranged so each loaded part has a slot with room to grow and all glyph data is in one region.
- Updates: the install percentage has its own line, and the player restarts by itself after an update.
- Folders list tracks by disc, number and file name again.

### Removed

- The listen history and `.scrobbler.log`.
- The built-in and 7-band Custom equaliser presets.
- The VU meter and LCD deck faces, whose animation never followed the music.
- Settings → Sound → Bluetooth level.

### Fixed

- FLAC files starting with an ID3 tag play.
- Corrupt FLAC and library files can no longer overflow the decoder or reader.
- The big scroll letter is drawn sharp from a 64 px font.

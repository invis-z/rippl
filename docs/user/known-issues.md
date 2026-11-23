# Known issues

Rippl is early firmware made by one person and tested on one player. Read this before you install.

## Safety

- **Volume:** the top volume is close to FiiO's maximum, and sensitive earphones get very loud. Turn it down before putting them in.
- **Battery:** Rippl switches off when the battery runs low, but doesn't yet refuse to start on an almost empty one. If it switches off by itself, charge it first.
- **Bricking:** custom firmware can leave an Echo Mini unable to start; maskrom is the way back ([recovery](../development/recovery.md)). A power cut during an install is a risk: install with the battery charged.

## Installing

- Tested on one Echo Mini 8 GB.
- FiiO's updater hasn't been tried with the 8 MiB `RIPPL.IMG`.
- Rippl can't be installed from EchoPod, which accepts only 5 MiB images ([install](install.md#from-echopod)).
- Going back to FiiO's firmware hasn't been tried with Rippl.
- Another custom firmware can't be installed from Rippl: go back to FiiO first.

## USB

- **Hi-Res DAC** is tested with Windows 11, up to 384 kHz 32-bit, but has had little testing overall.
- **DAC** has had little testing.
- The media keys and the plug-in prompt haven't been tested with Rippl.

## Music and library

- **Library size:** tested on the player with about 2,200 songs. The host tests build libraries of up to 250,000 tracks.
- **Characters:** Latin (with Latin Extended-A, Romanian and Vietnamese), Greek, Cyrillic, Korean (the 2,350 common syllables), Simplified Chinese (GB 2312), common Traditional Chinese (Big5 level 1) and Japanese (JIS X 0208) draw. Rarer characters, emoji and other scripts show as `?`. Han characters shared with Japanese use their Chinese style.
- **Covers:** a full card leaves new albums without art.
- **Chapters** come from the `chpl` list only (up to 255 per file, to the second, without names).
- **Equaliser load** has been measured only at 96 kHz 24-bit, with a 10-filter preset on white noise, the hardest case for FLAC: the second core was 82 % busy (16 % with the equaliser off).
- **Gapless MP3** needs a LAME tag. After a seek, that track's silent end isn't trimmed.
- After a firmware update, nothing plays until the player restarts, which it does by itself once USB is unplugged.

## Display

- Going back to an older Rippl after choosing a new theme resets all settings; choose Dark or Light first.
- The Now Playing face setting is gone: a face now comes with its theme, so after updating choose the Cassette, Turntable or Tape Window theme to get one back.
- Scenes and theme drawing haven't been timed on the player, and their battery cost hasn't been measured. If the face code can't be loaded from internal storage, Now Playing shows Standard until the next restart.
- The player has room for about 3,300 bytes of scene; themes made elsewhere with bigger scenes show the standard Now Playing.
- Settings → Theme lists at most eight theme files.

## Languages

- The translations haven't been reviewed by native speakers. Some long texts may wrap or be cut.
- A language change needs a restart.
- Some text stays English: the early Battery low notice, the panic screen, the drive names, the `Audiobooks` and `Podcasts` folder names, fallback names such as Unknown Artist, and Diagnostics' abbreviations.

## Tide

- **Untested.** Tide passes the host tests but hasn't been run on a real card or computer.
- FAT32 and exFAT only; on other file systems the player rescans anyway.

## Not there yet

- ReplayGain, playback speed, HE-AAC, APE and DSD.
- Bluetooth pairing with a PIN or code; LDAC.
- The equaliser for tracks at 176.4 kHz and above, for 32-bit tracks and in the DAC modes.

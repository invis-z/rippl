<h1 align="center"><img src="assets/rippl-title.svg" width="48" alt="" align="absmiddle"> Rippl</h1>

<p align="center">Replacement firmware for the Snowsky (FiiO) Echo Mini 8 GB music player.</p>

Rippl gives the Echo Mini an iPod-style Cover Flow browser, a music library built on the player, favourites, playlists and an Up Next queue, with gapless hi-res playback up to 384 kHz. It's a fork of [EchoPod](https://github.com/BasilSkyWalk/echopod) 0.12.0, and isn't made or supported by FiiO.

> [!WARNING]
> **Rippl is under active development, tested on one player, and comes with no warranty.** On-card file formats (`/.rippl`, themes, and settings) are unstable and may change or reset without migration between builds. Read the [known issues](docs/user/known-issues.md) before installing, and make sure you can [recover the player](docs/development/recovery.md).

## Features

- **Playback:** FLAC, ALAC, WAV, MP3, AAC, Opus and QOA, gapless, from 8 kHz to 384 kHz, 32-bit FLAC and ALAC up to 96 kHz. Low-rate files are upsampled with a proper filter.
- **Library:** Cover Flow, Artists, Album Artists, Albums, Genres, Folders, All Songs, M3U playlists and Favourites, built on the player and kept current by incremental rescans.
- **Now Playing:** the cover with lyrics from `.lrc` files, or one of three animated faces: Cassette, Turntable and Tape Window.
- **Queue:** shuffle by song or album, repeat, Up Next, and resume after power-off.
- **Equaliser:** parametric presets from Equalizer APO and AutoEQ files.
- **Audiobooks and podcasts:** their own shelves, with a bookmark for every book and episode, and `.m4b` chapters.
- **Bluetooth headphones:** pairing, six remembered devices, automatic reconnection.
- **USB:** both drives as mass storage, a USB DAC, or a Hi-Res DAC up to 384 kHz.
- **And more:** portrait mode, nine themes, a clock with daylight saving, 18 languages, CJK names, screen-off controls, and updates by copying a file.

[Tide](docs/user/tide.md), a companion tool for Windows, macOS and Linux, is meant to build the library on a computer so the player skips its own scan; it hasn't been tested yet.

## Getting started

1. Read the [installation guide](docs/user/install.md) and install `RIPPL.IMG`.
2. Put music on a microSD card and insert it.
3. Switch on. The player builds its library on the first start.

The [documentation](docs/README.md) covers everything else.

## Building

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset device && cmake --build --preset device
```

The first line builds the simulator and runs the tests; the second builds the firmware into `build/device/RIPPL.IMG`. See [building](docs/development/building.md) for requirements on macOS, Windows and Linux.

## Repository layout

| Path | Contents |
|---|---|
| `src/` | The player, shared by the firmware and the simulator: app, UI, library, playback, codecs, DSP, graphics, fonts |
| `platform/device/` | The firmware: board drivers, core 1 decoder programs, overlays |
| `platform/sim/` | The SDL2 simulator |
| `tests/` | Host tests, run with `ctest` |
| `tools/` | Image, font and language pack builders, Tide, the PC indexer |
| `assets/` | Fonts, translations, artwork |
| `docs/` | [Documentation](docs/README.md) |

## Status

Rippl is a personal project. Issues and pull requests are turned off, and there's no support, roadmap or release schedule. You're welcome to fork it under its [licence](LICENSE); the [development docs](docs/development) cover building, testing and the project's conventions.

## Licence

GPL-3.0-or-later; see [LICENSE](LICENSE). Bundled fonts and libraries keep their own licences; [CREDITS.md](CREDITS.md) lists them and the projects Rippl builds on.

## AI-assisted code and licensing

Rippl's modifications to EchoPod were developed with the assistance of a large language model (LLM). Depending on the jurisdiction, such code may not be attributable to a human author, and its copyright status, including whether it is protected at all or in the public domain, may be uncertain.

Before redistributing this code or incorporating it into a commercial or otherwise sensitive project, have it reviewed by a qualified legal professional, and confirm that the notices, licences and attribution requirements in [LICENSE](LICENSE) and [CREDITS.md](CREDITS.md) are satisfied.

This notice is provided for general awareness only and does not constitute legal advice.

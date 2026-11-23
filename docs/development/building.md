# Building

Rippl builds with CMake. One tree produces the firmware, an SDL2 simulator for macOS, Windows and Linux, and the host tests.

## Requirements

- CMake 3.25 or later, a C11 compiler (Clang or GCC) and SDL2 for the simulator.
- Arm's GNU toolchain (`arm-none-eabi-gcc` with newlib) for the firmware.
- Python 3 with `tools/requirements.txt` (images, fonts, language packs, Tide) and, for packaging Tide, `tools/requirements-build.txt`.
- `clang-format` for the formatting check.

### macOS

```sh
brew install cmake sdl2 clang-format
brew install --cask gcc-arm-embedded
python3 -m venv .venv && .venv/bin/pip install -r tools/requirements.txt -r tools/requirements-build.txt
```

Homebrew's `arm-none-eabi-gcc` formula ships without newlib headers (`math.h: No such file or directory`); use Arm's `gcc-arm-embedded` cask, and uninstall the formula if it's there.

### Windows

Install [MSYS2](https://www.msys2.org) and, in its CLANG64 shell, `mingw-w64-clang-x86_64-{clang,compiler-rt,cmake,ninja,SDL2}` and `mingw-w64-clang-x86_64-arm-none-eabi-{gcc,binutils,newlib}`, plus Python 3 with `pip install -r tools/requirements.txt`. Put `C:\msys64\clang64\bin` first on the PATH, ahead of `C:\msys64\usr\bin` (whose git ignores this repository's line endings), and configure with `CMAKE_GENERATOR=Ninja CC=clang`.

### Linux

Install CMake, Clang or GCC, SDL2's development package, `clang-format`, and Arm's GNU toolchain with newlib (for example `gcc-arm-none-eabi` and `libnewlib-arm-none-eabi` on Debian and Ubuntu), then the Python packages as on macOS.

## Presets

| Preset | Output | Purpose |
|---|---|---|
| `dev` | `build/dev` | Simulator and host tests, Debug with sanitisers |
| `device` | `build/device` | Firmware |

```sh
cmake --preset dev && cmake --build --preset dev && ctest --preset dev
cmake --preset device && cmake --build --preset device
```

A firmware build writes `RIPPL.IMG` (8 MiB) and prints how full each memory region is. [Device testing](device-testing.md) covers installing it.

The `dev` build also packs the themes from `assets/themes` into `build/dev/themes` (including `cassette.rippltheme`, `turntable.rippltheme`, `tape_window.rippltheme`), ready to copy to a card's `Themes` folder.

## Tests

`ctest --preset dev` runs the host tests: unit and app tests against a fake HAL, the formatting check, the firmware build and its memory audit, and checks that generated files (language packs, glyph packs, test libraries) are up to date. To regenerate those, run the tool the failing test names, for example:

```sh
python3 tools/lang_pack.py --catalogue src/core/include/core --languages assets/lang --output src/lang/packs
python3 tools/glyph_pack.py --config assets/fonts/glyph_pack.json --output src/fonts/packs
```

## Simulator

```sh
mkdir -p build/system
./build/dev/platform/sim/rippl_sim --card /path/to/music --system-dir build/system
```

The simulator behaves like the player: on the first start it creates the save files in the system folder, scans the music folder and builds the covers. The music folder is only read.

| Option | Effect |
|---|---|
| `--card DIR` | Use DIR as the card's root (read only) |
| `--system-dir DIR` | Read and write `/.rippl/` in DIR; without it, nothing is saved |
| `--scale N` | Window scale (default 3) |
| `--headless` | No window; time is virtual, so runs are deterministic |
| `--frames N` | Quit after N frames |
| `--screenshot FILE.bmp` | Save the last frame on exit |
| `--keys LIST` | Play a key script, for example `down,down,play,back:long` |
| `--battery N`, `--charging` | Report the battery level and charging |
| `--clock-date YYYY-MM-DD` | Start the clock chip on that date |

| Player | Keyboard |
|---|---|
| Volume up (moves down in lists) | Down arrow |
| Volume down (moves up in lists) | Up arrow |
| ▶‖ | Return or Space |
| ≡ | M or Tab |
| ↩ | Backspace or Escape |
| Power | P |

## Tide

Run Tide from the source with `python3 tools/tide`. The release workflow (`.github/workflows/tide.yml`) packages it with PyInstaller for Windows, macOS and Linux; to do the same locally:

```sh
python -m PyInstaller --onefile --name tide --paths tools \
  --add-data assets/lang/languages.json:assets/lang \
  --add-data src/lang/packs:src/lang/packs tools/tide/__main__.py
```

On Windows, write `;` instead of `:` in the `--add-data` values. `tools/library_index.py` still writes the older `library.db`, the reference the device builder is tested against.

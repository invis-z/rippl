# Tide

> **Untested.** Tide passes the host tests but hasn't been run on a real card or computer yet.

Tide is Rippl's companion tool for a computer. It builds the library and covers straight onto the card, sets the player's clock, installs language packs and lists audiobook bookmarks. A synced card needs no rescan on the player, and later rescans read only new files.

## Get Tide

Download it from the release's assets. It's a single file with nothing to install:

| Computer | File |
|---|---|
| Windows | `tide-windows-x64.exe` |
| Mac with Apple silicon | `tide-macos-arm64` |
| Linux (x86-64) | `tide-linux-x64` |

- **Windows:** double-click it. If Windows "protected your PC" (the file isn't signed), click **More info**, then **Run anyway**.
- **Mac:** allow it once, then run it:
  ```sh
  chmod +x ~/Downloads/tide-macos-arm64
  xattr -d com.apple.quarantine ~/Downloads/tide-macos-arm64
  ```
- **Linux:** `chmod +x tide-linux-x64`.
- **Other computers, or from the source:** install Python 3 and the packages once (`python3 -m pip install -r tools/requirements.txt`), then run `python3 tools/tide`. On Windows, `tools\tide.bat` finds Python for you.

## Sync a card

1. Copy your music to the card, in the player over USB or in a card reader.
2. Run Tide. It finds a card the player has used and asks "Card to sync": press Enter, or type or drag another drive first. From a terminal: `tide sync E:` (Windows) or `tide sync /Volumes/CARD`.
3. When it says "Done", eject the card (in the player: both drives).

Tide reads the whole card as the player does and writes the library, covers and scan records into `/.rippl`, plus the fingerprint `scan.bin`. It writes only to segments the player isn't using and switches over at the very end, so stopping it half-way leaves the player's library as it was. Covers whose art didn't change are kept. It then reports what it found, for example "1,402 tracks, 120 albums, 3 playlists".

- **Music folders:** folders left out in Settings → Library → Music folders are left out here too.
- **Internal storage:** with the card in the connected player, Tide reads internal storage only if it finds a drive named RIPPL. The drive is named ECHO MINI until you rename it, so rename it once on the computer, or pass `--internal F:`. Without it, Tide builds from the card only and the player rescans afterwards to add internal storage's music.
- **Clock:** Tide writes the computer's time and daylight saving rule, which the player takes when the USB session ends. A card synced in a card reader doesn't set the clock.
- **EQ presets:** Tide fits each preset in `/EQ` that has no fit yet to every sample rate, as the player would but more closely, and deletes the fits of presets that are gone, so the player has nothing left to fit. A preset takes a few seconds. Without numpy (running from the source), the player fits them itself.

### Why the player doesn't rescan

After a USB session the player compares the music's names, sizes and times with the fingerprint in `scan.bin`. Tide writes the same fingerprint, so nothing looks new. The player rescans anyway, losing nothing, when:

- the music changed after Tide ran;
- an exFAT file time was saved without a time-zone tag (some cameras and old devices);
- the card is FAT32 and the computer is a Mac or Linux (not yet checked against the player);
- folders are more than 31 levels deep or paths longer than 1,023 bytes;
- a name was saved decomposed (for example by Linux) and read on a Mac, or only as a DOS short name with accented letters.

`tide sync E: --check` after a scan on the player tells you whether your card works: "They match" means no rescan after the next sync.

## Options

| Option | Effect |
|---|---|
| `--dry-run` | Read everything and report, but write nothing |
| `--no-covers` | Keep the card's covers without looking for art |
| `--no-fingerprint` | Leave `scan.bin` alone |
| `--internal DRIVE` | The internal storage drive, when it isn't named RIPPL |
| `--no-clock` | Don't set the player's clock |
| `--no-eq` | Don't fit the EQ presets; the player fits them itself |
| `--check` | Only compare fingerprints |
| `--yes` | Create `/.rippl` without asking |
| `--verbose` | List albums without art |

## Other commands

| Command | Effect |
|---|---|
| `tide languages` | List the language packs |
| `tide languages install <code>` | Copy a pack to the player's internal drive |
| `tide bookmarks <card>` | List audiobook and podcast bookmarks with positions; `--finished` lists finished ones, `--json` prints JSON |

`tide` with no command runs `sync`.

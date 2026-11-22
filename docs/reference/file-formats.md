# File formats

The files Rippl keeps in `/.rippl` on the card. Everything is little-endian. Checksums are 32-bit FNV-1a over the bytes before them unless noted. A missing, zero-filled or damaged file reads as empty. How the library is built is in [library internals](../development/library-internals.md).

## Overview

| File | Written by | Holds |
|---|---|---|
| `manifest.bin` | Player, Tide | The segment store's manifest |
| `library/` | Player, Tide | 1 MiB segment files: the library, scan records, builder scratch |
| `covers/` | Player, Tide | 4 MiB segment files: album covers |
| `scan.bin` | Player, Tide | The music fingerprint the player compares after USB |
| `equaliser/` | Player, Tide | Equaliser presets fitted to each sample rate, and the fit log |
| `settings.bin`, `bluetooth.bin` | Player | Settings, remembered headphones |
| `equaliser.bin` | Player, Tide | The last preset, and which fit slot holds each preset's key |
| `resume.bin`, `upnext.bin` | Player | The playing track and queue, and Up Next |
| `favourites.bin`, `bookmarks.bin` | Player | Favourite albums, audiobook and podcast positions |
| `folders.bin` | Player | Top-level folders left out of the library |
| `update.bin` | Player | What the firmware installer last did |
| `clock.bin` | Tide | The computer's time and daylight saving rule |
| `errors.log` | Player | Text: files the player couldn't open or use |
| `theme.bin` | Player | The theme file chosen in Settings |

## Segment store

Data lives in **streams**: chains of fixed-size segment files, 1 MiB in `library/` (up to 512) and 4 MiB in `covers/` (up to 256), reserved when first needed and reused after that, never deleted.

`manifest.bin` holds two 4 KB copies of the manifest: a sequence number, each stream's pool, first segment, segment count and length, 16 regions (stream, offset, length), 8 values, the next-segment table and a checksum. The newer valid copy wins.

A change is a transaction: begin, append to streams and write only bytes the committed manifest doesn't use, then commit by writing the other manifest copy. New data goes only to unused segments or past a committed stream's end, so power lost at any moment leaves either the old state or the new one.

| Stream | Region | Content |
|---|---|---|
| 0 | 2 | Cover table |
| 1 | 0 | Library |
| 2, 3 | | Scan records, taking turns (value 0 names the current one) |
| 4, 5 | | Cover records, taking turns (value 1 names the current one; value 2 counts slots) |
| 7 | | Builder scratch, dropped at commit |

## Library

The library is region 0. Indices are `u32`; a folder's parent of `0xFFFFFFFF` marks the root.

**Header** (256 bytes): magic `RLIB`, `u16` version 1, `u16` section count 13, then thirteen 16-byte entries of `u32 offset`, `u32 count`, `u16 record size`, `u16` 0, `u32 music count`. The music count marks where music ends and spoken word begins in tracks, albums, artists, album artists, genres and title order.

| # | Section | Size | Content |
|---|---|---|---|
| 0 | Tracks | 32 | Grouped by album in album order, then disc, number and title |
| 1 | Albums | 24 | Sorted by title, then album artist |
| 2 | Artists | 12 | Sorted by name |
| 3 | Album artists | 12 | Sorted by name |
| 4 | Genres | 12 | Sorted by name |
| 5 | Folders | 20 | Depth first; folder 0 is the root |
| 6 | Title order | 4 | Track indices by title, then artist (All Songs) |
| 7 | Playlists | 12 | Sorted by name |
| 8 | Links | 4 | Shared index lists that groups, playlists and folders point into |
| 9 | Strings | 1 | NUL-terminated UTF-8 (NFC); offset 0 is the empty string |
| 10 | Path IDs | 12 | `u64 id`, `u32 track`, sorted by ID, then track |
| 11 | Album keys | 12 | `u64 key`, `u32 album`, sorted by key, then album |
| 12 | Album ranks | 4 | Each album's position among the album keys |

| Record | Layout |
|---|---|
| Track | `u32 title`, `u32 path`, `u32 album`, `u32 artist`, `u32 genre`, `u32 folder`, `u32 duration_ms`, `u8 disc`, `u8 number`, `u16` 0 |
| Album | `u32 title`, `u32 album_artist`, `u32 genre`, `u32 first_track`, `u32 track_count`, `u16 year`, `u16` 0 |
| Group (artist, album artist, genre, playlist) | `u32 name`, `u32 first_link`, `u32 link_count` |
| Folder | `u32 name`, `u32 parent`, `u32 subfolder_count`, `u32 first_link`, `u32 track_count` |

- String fields are offsets into the strings section.
- A track `path` is relative to the music root, with `/`. Tracks on internal storage are under `:internal/`, a name no FAT or exFAT card can hold.
- An album's tracks are `tracks[first_track ..+ track_count]`. A group's links are album indices (artists and album artists by year then title, genres by title); a playlist's are track indices in playlist order; a folder's are its subfolders, then its tracks.
- A **path ID** is the 64-bit FNV-1a of the track's path with ASCII letters lowercased, so a file keeps its ID across rescans. An **album key** is the 64-bit FNV-1a of the album artist, `0x1F` and the title.

### Legacy library.db

The PC indexer (`tools/library_index.py`) still writes `/.rippl/library.db`, version 3, as the reference the device builder is tested against. It has the same sections 0–9 with 16-bit indices (`0xFFFF` is none, at most 65,535 of anything), a 128-byte header (magic `EMDB`, `u16` version 3, `u16` section count 10, ten 12-byte entries of `u32 offset`, `u32 count`, `u16 record size`, `u16 music count`), and these records:

| Record | Size | Layout |
|---|---|---|
| Track | 24 | `u32 title`, `u32 path`, `u16 album`, `u16 artist`, `u16 genre`, `u16 folder`, `u32 duration_ms`, `u8 disc`, `u8 number`, `u16 chapters` |
| Album | 16 | `u32 title`, `u16 album_artist`, `u16 genre`, `u16 year`, `u16 first_track`, `u16 track_count`, `u16 cover` (reserved) |
| Group | 12 | `u32 name`, `u32 first_link`, `u16 link_count`, `u16` reserved |
| Folder | 16 | `u32 name`, `u16 parent`, `u16 subfolder_count`, `u32 first_link`, `u16 track_count`, `u16` reserved |

## Covers

Each cover is 112 × 112 pixels followed by 56 × 56, little-endian RGB565 rows: 31,360 bytes. Slot `n` sits at `(n / 133) × 4 MiB + (n % 133) × 31,360` in the cover stream, 133 to a segment.

The **cover table** has one 8-byte row per album key position: `u32 slot` (`0xFFFFFFFF` for none) and `u32 stamp`. Drawing an album reads its rank, then that row.

A **stamp** is FNV-1a over where the art came from (`/` plus the path for embedded art, the folder plus the file name for a cover file), then three `u32`: for embedded art the track's size, the picture's length and 1; for a cover file its size, its modification stamp and 2. A stamp of 0 means unknown.

## Scan records

One record per file, in walk order: `u16 length`, `u8 kind` (1 track, 2 unreadable), `u8 flags` (bit 0: album artist guessed), `u32 size`, `u32 stamp`, `u32 duration_ms`, `u16 year`, `u8 disc`, `u8 number`, `u8 chapters`, then NUL-terminated texts: the path and, for a track, title, artist, album artist, album and genre. At most 4,096 bytes per record.

## Playback

| File | Layout |
|---|---|
| `resume.bin` | Magic `EMRS`, version 1, 60 bytes: the playing track's path ID, position, the queue plan and the library fingerprint |
| `upnext.bin` | Magic `EMUN`, version 1, 812 bytes: `u16` count, up to 100 path IDs (`u64`), checksum |
| `favourites.bin` | Magic `EMFV`, `u16` version 1, `u16` count, up to 256 album keys (`u64`), checksum |

### bookmarks.bin

4 KB, keyed by path hashes so it survives a rescan.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | `EMBK` |
| 4 | `u16` | Version, 1 |
| 6 | `u16` | Count, at most 128 |
| 8 | `u32` | Reserved |
| 12 | 16 × count | Bookmarks, most recent first |
| 12 + 16 × count | `u32` | Checksum |

A bookmark is `u32 key`, `u32 track key`, `u32 position_ms`, `u16 chapter`, `u8 flags` (bit 0: finished), `u8` reserved. The key hashes the book's folder path (`Audiobooks/The Long Road`), or the track's own path for a single-file book or an episode; the track key hashes the track's path.

## Equaliser fits

`equaliser/E00.BIN` to `E99.BIN` each hold one preset's fits: one per sample rate where the fit beats the preset's usual filters. A slot is 4 KB. The player takes the lowest slot that's missing, zero-filled or holds a preset no longer in `/EQ`, and zero-fills the slots of presets that are gone, since it can't delete files; Tide deletes them.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `EQFT` |
| 4 | 1 | Version, 1 |
| 5 | 1 | Entry count, up to 11 |
| 8 | 4 | Curve key |
| 12 | 4 | Target rate (Hz) |
| 16 | 178 × n | Entries |
| | 4 | Checksum over everything before it |

The curve key is FNV-1a over the fit method (2) and the target rate, then each filter's frequency, gain in tenths of a dB, Q in hundredths, type and position. A file whose key matches no preset, or whose checksum fails, is unused.

An entry is the rate (`u32`), the worst error in hundredths of a dB (`u16`), the stage count (`u8`), a spare byte, and 10 stages of 17 bytes: `g`, `a1`, `m1` and `m2` as `i32`, then the filter number, with `0x80` set for a stage that uses fine coefficients (`g` in Q20 and `1 − a1` in Q18 instead of Q14).

`equaliser.bin` keeps the chosen preset in its first 75 bytes. From byte 75 it holds the fit map: 100 curve keys as `u32`, one per slot (0 when the slot holds no fit), then a checksum over those 400 bytes. The player writes the map after it has checked every slot, and Tide after a sync; neither changes the other part. To use a preset, the player looks its key up in the map and checks that slot's key and checksum. A key the map doesn't hold has no fit. A missing or damaged map, or a slot that fails its check, means the player searches the slots instead.

`equaliser/FIT.LOG` is text, written only while Diagnostics → Fit log is on: for each fitted rate its error, the usual filters' error, the time, starts, rounds and CPU clock, and for each preset its slot, rate count, worst error and total time. It stops at 64 KB.

## folders.bin

4 KB reserved, 140 bytes used.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `EMMF` |
| 4 | 1 | Version, 1 |
| 5 | 1 | Count, at most 32 |
| 6 | 2 | Reserved |
| 8 | 128 | 32 folder keys (`u32`), the first `count` used |
| 136 | 4 | Checksum |

A folder key is the FNV-1a of the folder's library path: its name for a card folder, `:internal/` plus its name for an internal storage folder.

## update.bin

4 KB reserved, written at the end of a USB session that found an image, shown in Diagnostics → Last update.

| Offset | Type | Field |
|---|---|---|
| 0 | `char[4]` | `EUPD` |
| 4 | `u8` | Result: 0 none, 1 installed, 2 failed, 3 refused, 4 already installed |
| 5 | `u8` | Last progress mark (5 % steps), or the refusal code |
| 6 | `u8` | 1 when internal storage answered CMD13 afterwards |
| 7 | `u8` | Reserved |
| 8 | `u32` | The eMMC status word from CMD13 |
| 12 | `u32` | Bitwise complement of a 31-multiplier sum over bytes 0–11 |

## clock.bin

32 bytes, written by Tide at the end of a sync, read by the player when the USB session ends. The player never writes it.

| Offset | Size | Field |
|---|---|---|
| 0 | 4 | `ECLK` |
| 4 | 1 | Version, 1 |
| 5 | 1 | Flags: bit 0, a daylight saving rule follows |
| 6 | 2 | Reserved |
| 8 | 4 | The computer's time, Unix seconds (UTC) |
| 12 | 2 | Standard UTC offset in minutes, signed |
| 14 | 2 | Minutes daylight saving adds, 1–180 (0 without a rule) |
| 16 | 6 | Start: month, week (1–4, 5 = last), weekday (0 = Sunday), 0, then `i16` minutes after midnight in standard time |
| 22 | 6 | End, the same way |
| 28 | 4 | Checksum |

The player accepts a record with a time in 2024–2099, an offset from −12:00 to +14:00 and a valid rule, once per new record. It sets the chip to standard local time, stores the offset as its time zone and sets Daylight saving to From computer (Off without a rule).

## theme.bin

28 bytes: the magic `RPLC`, then the chosen theme file's name without `.rippltheme`, in 24 bytes, zero-filled. Empty when a built-in theme is chosen; which one is in `settings.bin`.

## Theme files

A `.rippltheme` file lives in `Themes` at the top of the card, and the built-in themes are the same files inside the image. They are packed by `tools/theme_pack.py` from TOML sources ([theme format](theme-format.md)), little-endian:

| Bytes | Holds |
|---|---|
| 0–3 | `RPLT` |
| 4–5 | Version, 1 |
| 6–7 | Header size, 48, or 52 when there are assets |
| 8–11 | Size of the checked part (header, tables and icons), at most 3,072 |
| 12–15 | FNV-1a of the checked part from byte 16 |
| 16– | Colour, box, spacing and icon counts, the selection box, the name in 24 bytes |

Then the colours (RGB565), boxes (11 bytes each), spacings, the asset table, accents and icon masks, and after the checked part the assets: wallpapers (16 colours, 4 bits a pixel) and fonts. Each asset carries its own FNV-1a. Ids are only ever added at the end, so older and newer firmware read each other's files and ignore what they don't know.


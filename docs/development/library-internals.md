# Library internals

How the player turns a card into the library described in [file formats](../reference/file-formats.md), and how that's tested. The PC indexer (`tools/library_index.py`) and the device builder (`src/library`) must produce libraries that mean the same, field by field.

## Scanning

`library/library_scan` walks the card through a `ScanFileSystem`: each folder is read whole, names starting with `.` are dropped, the rest are sorted by their UTF-8 bytes, and the walk goes depth first without recursion, one folder open at a time. Every listing keeps each entry's size and modification stamp.

Files with these extensions are listed: `.flac`, `.ogg`, `.opus`, `.mp3`, `.m4a`, `.m4b`, `.wav`, `.wma`, `.ape`, `.dsf`, `.dff`, `.qoa`. Each is read by `track_tags_read`, given fallbacks by `library/track_fields`, and passed to the builder. A file whose tags can't be read is skipped and counted.

**Scan cache.** A rescan merges the previous scan's records with the walk like a sorted merge: a record with the same path, size and stamp is used instead of opening the file, records whose path sorts before the current file are dropped, and every file found writes a new record.

**On the player**, `library/library_rebuild` runs the rescan as one segment-store transaction inside the rescan overlay, with the cover pass in the same transaction. If it fails, or the card is full ("No room for the library on the card"), the transaction is cancelled and the old library stays. The store's sequence number is mixed into the library fingerprint, so resume notices any rescan.

## Tags

Each field takes the first non-empty value, trimmed:

| Field | Vorbis comment | ID3 | MP4 | APEv2 | ASF |
|---|---|---|---|---|---|
| title | `title` | `TIT2` | `©nam` | `Title` | `Title` |
| artist | `artist` | `TPE1` | `©ART` | `Artist` | `Author` |
| album artist | `albumartist` | `TPE2` | `aART` | `Album Artist` | `WM/AlbumArtist` |
| album | `album` | `TALB` | `©alb` | `Album` | `WM/AlbumTitle` |
| genre | `genre` | `TCON` | `©gen` | `Genre` | `WM/Genre` |
| date | `date` | `TDRC` (`TYER`) | `©day` | `Year` | `WM/Year` |
| disc | `discnumber` | `TPOS` | `disk` | `Disc` | `WM/PartOfSet` |
| track | `tracknumber` | `TRCK` | `trkn` | `Track` | `WM/TrackNumber` |
| album version | `albumversion` | `TXXX:ALBUMVERSION` | `----:ALBUMVERSION` | `AlbumVersion` | — |

Vorbis comments cover FLAC, Ogg and Opus; ID3 covers MP3, WAV, DSF and DFF (ID3v1 when there's no v2). With an album version, the album becomes `Album (Version)`, so two editions are two albums. QOA has no tags: its fields come from the path.

Values keep their first 255 bytes as trimmed UTF-8; control codes are dropped, bytes 0x80–0x9F read as Windows-1252, and typographic spaces become plain spaces. Durations are computed in double precision in mutagen's order, so the rounded milliseconds match the PC. Known differences: the device doesn't recompose values to NFC, and doesn't undo tag-wide unsynchronisation in ID3v2.3.

`tools/tag_fixture.py` writes one small file per format to `tests/fixtures/tags`; both readers are tested against them.

### Fallbacks

| Missing | Used instead |
|---|---|
| title | The file name without its number prefix |
| artist | The album artist, then "Unknown Artist" |
| album artist | The artist, shared per album (below) |
| album | The folder name ("Unknown Album" in the root or `:internal`) |
| genre | "Unknown Genre" |
| disc, number | A `<disc>-<track> - ` or `<track> - ` file-name prefix, else disc 1, track 0 |
| year | The first four digits of the date, else 0 |

Tracks without an album-artist tag that share a folder and album title get the artist most of them have, so featured artists don't split an album.

## Sorting and spoken word

Sort keys are case- and accent-insensitive (NFKD with combining marks removed), and a leading "The " is ignored.

A track is spoken word when its first folder is `Audiobooks`, `Audiobook`, `Podcasts` or `Podcast` (ASCII case-insensitive). Every key of a spoken track starts with a flag that sorts after music, so spoken word never shares an album or group with music and sits after it in each section; the header's music count marks the boundary. The Music lists read only the music part; folders and playlists show everything.

## Building

`library/library_builder` builds from tracks in scan order with a fixed block of RAM (360 KB on the player) and scratch space on the card. Nothing in RAM grows with the number of tracks, albums or folders: wherever a step needs values by number, they go to the card as (number, value) pairs and come back through an external merge sort.

1. **Scan:** each track becomes a 48-byte row on the card with its texts in a heap; folders become rows with a 64-bit path hash, renumbered after the scan if siblings arrived out of order.
2. **Album artists:** tracks without the tag get their album's shared artist in a sort-and-merge pass.
3. **Ranking:** every field is ranked by an external merge sort (sorted chunks in RAM, up to 32-way merges), giving a dense key rank per track.
4. **Views:** albums, track order, group links, folders and All Songs are built from integer tuples with the same sort, in the PC's order. Playlists resolve through tables of casefolded path and file-name hashes, confirmed against the real paths.
5. **Write:** `library_builder_finish_wide` writes the library; `library_builder_finish` writes the legacy format and refuses more than 65,535 of anything. The magic goes in last, so an interrupted build reads as no library.

## Playlists

Every `.m3u` and `.m3u8` outside hidden folders is read as UTF-8 (BOM optional), or as Windows-1252 if that fails. Blank lines, `#` lines and URLs are skipped. Each entry is matched ignoring case, with `\` as `/`:

1. relative to the playlist's folder;
2. relative to the music root;
3. by the longest run of trailing path parts, so absolute Windows paths match. A tie counts as not found.

On the device, playlists are read after the tracks, once the builder has fixed the track order; entries are resolved as they're read and never stored.

## Covers

For each album, the cover is the embedded picture of the first of its first four tracks that has one (a front cover, picture type 3 or MP4's `covr`, wins), otherwise a `cover`, `folder`, `front` or `albumart` JPEG or PNG in a folder holding its tracks. It's cropped to a centred square and resized for each size.

A rescan walks the new and old libraries' album keys side by side: an album whose key and stamp match keeps its slot, new or changed art is decoded and appended, and slots of albums that are gone become garbage. When more than a quarter of the slots are garbage, the next rescan copies the live covers into the other stream without decoding. A full card leaves new albums without art and keeps the rest.

## Tide

Tide (`tools/tide`) writes the same store from a computer: `store.py` handles the manifest and segments, `rippl_format.py` writes the library from the PC indexer's model, `records.py` writes scan records in the player's walk order, and `covers.py` builds covers with Pillow, keeping those whose key and stamp match the card's.

## Tests

| Test | Checks |
|---|---|
| `test_library_builder` | Random libraries of 1 to 20,000 tracks against the in-RAM reference builder, field by field; every track found by path ID |
| `test_large_library` | 250,000 tracks through segment scratch into a stream, within 360 KB of RAM |
| `test_library_scan` | The fixtures scanned twice (the second opens no file), with folders left out, and with a full card |
| `test_segment_store` | Power cut at every write, with torn writes |
| `test_cover_build`, `test_covers` | Cover reuse, replacement, removal, compaction and a full card |
| `cover_build_matches_reference` | Covers for real art against Pillow, and a Tide-synced card against the player's own build |
| `tags_library_up_to_date` | `tests/fixtures/tags_library.db` matches a fresh PC index |

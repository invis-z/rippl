# Your music library

Where Rippl looks for music, how it builds the library, and how to control what's in it.

## Putting music on the player

- **microSD card:** FAT32 or exFAT, any folder layout. A card is required even if all your music is on internal storage, because the library lives on the card.
- **Internal storage** (about 8 GB; the computer shows it as ECHO MINI unless you renamed it): its music joins the card's everywhere. Under Folders it appears as one more folder, **Internal storage**. The player never writes to internal storage.
- **Playlists:** `.m3u` and `.m3u8` files anywhere on the card appear under Music → Playlists. Entries are matched relative to the playlist, to the card's root, or by their trailing path parts, so absolute Windows paths (as MusicBee writes) work too.
- **Covers:** art inside one of an album's first four tracks (a front cover wins), otherwise a `cover`, `folder`, `front` or `albumart` JPEG or PNG next to the tracks.
- **Ignored:** hidden files and folders, firmware images and other files that aren't music or playlists.

If the same album is on the card and on internal storage, both copies are in the library and show as one album with every track twice; leave one copy's folder out (below).

The [formats Rippl plays](playback.md#supported-formats) are listed with playback.

## Building the library

The player scans on the first start, after music changes over USB, and from Settings → Library → **Rescan library**. A rescan reads only new or changed files and keeps your place: the song you were playing comes back paused, and Up Next keeps its songs. Covers whose art didn't change are kept.

Most of a first scan goes to decoding covers. The player is busy until it finishes.

Missing tags fall back sensibly: the file name for the title, the folder name for the album, the artist for the album artist. Tracks of one album where only some name a featured artist still group as one album.

## Music folders

Settings → Library → **Music folders** lists the top-level folders of the card and internal storage, all ticked at first. ▶ toggles a folder; leaving with Back saves the choice and rebuilds the library without the unticked folders. The row shows **All** or a count such as **3 of 5**.

- Music directly in a root folder always stays in the library.
- `Audiobooks` and `Podcasts` aren't listed: their shelves always read them.
- Up to 32 folders are listed. The choice is kept by folder name in `/.rippl/folders.bin`, so new folders are ticked and a renamed folder counts as new.

## Browsing

- **Cover Flow** shows every album by album artist (Settings → Library → Cover Flow order switches to album title), with 5 or 3 covers on screen. Vol± move, ▶ opens the album, holding ▶ adds or removes it from Favourites.
- **Artist albums → Cover Flow** opens an artist, album artist or genre as a Cover Flow of their albums.
- **Folders & playlists → Cover Flow** opens a playlist as a Cover Flow of its albums in playlist order, and a folder that holds only folders as a Cover Flow of those folders (each shows the cover of its first song's album). A folder with songs opens as a list.
- A–Z lists support fast scroll with letter jumps.

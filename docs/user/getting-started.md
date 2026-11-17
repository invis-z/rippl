# Getting started

The basics: what the player does on its first start, how the buttons work, and where things are.

## First start

Rippl needs a microSD card (FAT32 or exFAT). The library, settings, favourites and resume points live in a hidden `/.rippl` folder on it: about 13 MB for a small library, growing with the music and covers. Don't delete it unless you want the library rebuilt.

On the first start with a card, the player creates `/.rippl`, scans the music ("Scanning library", then "Finding covers") and shows "Library: N tracks, M covers". Without a card, Home says "No microSD card" and changed settings aren't kept.

After installing a new firmware, the player checks the music once ("Checking for changes") and rescans only if something changed. If the clock isn't set, it then asks once whether to set the date and time.

## Buttons

| Button | In lists | On Now Playing |
|---|---|---|
| Volume up / down | Next / previous item; hold to scroll faster | Volume; hold Vol+ for the next track, Vol− for the previous one (in audiobooks and podcasts: 30 s forward, 15 s back) |
| ▶‖ Play | Select; a song plays the list from that song. Hold on an album for Favourites, on a song for Play Next / Add to Up Next | Play / pause; hold to seek |
| ≡ Menu | Hold for Settings | Up Next; hold for the Equaliser (when `/EQ` has presets) |
| ↩ Back | Back; hold to go home | Back; hold to go home |
| Power | Screen off or on; hold 2 s to turn off | Same |

With the screen off, ▶‖ still pauses and resumes, a tap of the volume rocker changes the volume, and holding it skips tracks. Settings → Buttons changes what the rocker and ≡ do ([settings](settings.md#buttons)).

After 30 s paused the player powers its audio output down to save battery, so resuming after a longer pause takes a moment longer.

## Home

Home lists **Music**, **Now Playing** and **Settings**, **Diagnostics** when it's turned on ([settings](settings.md#diagnostics)), and **Audiobooks** and **Podcasts** when the card has those folders ([audiobooks and podcasts](audiobooks-podcasts.md)). The panel beside the menu shows the current album's cover.

Music opens Cover Flow, Albums, Artists, Album Artists, Genres, Folders, All Songs, Playlists and Favourites ([library](library.md)).

- **Play all / Shuffle all** sit at the top of an artist's, album artist's or genre's album list; All Songs and playlists have **Shuffle all**. Lists open on the first item, so scroll up to reach them.
- **Random album** is the tile before the first album in Cover Flow: ▶ plays a random album from its first track.

## Portrait mode

Settings → Display → Orientation offers Landscape, Portrait and Portrait, flipped; pick whichever puts the buttons where you like them. In portrait, Home shows the album art under the menu, Now Playing puts the cover above the track, and Cover Flow scrolls top to bottom. The volume rocker turns with the screen for lists; a tap turns the volume the other way from landscape, while holding still skips and seeks as in landscape.

## Charging

The player charges whenever it's plugged in, on or off. Turning it off while plugged in, or plugging a charger into a switched-off player, shows the charge screen (the battery level, or "Charged"). It goes dark after 5 seconds; any button shows it again. Hold Power for 2 seconds to turn the player on. Plugging into a computer opens USB mode ([USB](usb.md)).

# Playback

Now Playing, the queue, lyrics, the equaliser and the formats Rippl plays.

## Supported formats

| Format | Extension | Sample rates | Bit depth | Notes |
|---|---|---|---|---|
| FLAC | `.flac` | 8–384 kHz | up to 24-bit; 32-bit up to 96 kHz | Blocks up to 16,384 samples |
| ALAC | `.m4a` | 8–384 kHz | up to 24-bit; 32-bit up to 96 kHz | Mono and stereo |
| WAV | `.wav` | 8–384 kHz | up to 32-bit | 32-bit goes to the DAC whole |
| MP3 | `.mp3` | 8–48 kHz | — | Gapless with a LAME tag |
| AAC | `.m4a` | 8–48 kHz | — | AAC-LC, mono and stereo; HE-AAC is refused |
| Opus | `.opus` | 48 kHz | — | Mono and stereo, packets up to 60 ms |
| QOA | `.qoa` | 8–48 kHz | 16-bit | Names come from the path; QOA has no tags |

- Files at 8–24 kHz are raised with a 64-tap filter: 8 and 16 kHz to 32 kHz, 11.025, 12, 22.05 and 24 kHz to 44.1 or 48 kHz, each 2 or 4 times. 32 kHz plays natively.
- `.m4b` audiobooks play as AAC or ALAC, with chapters ([audiobooks and podcasts](audiobooks-podcasts.md)).
- APE, WMA, DSF, DFF and Ogg Vorbis appear in the library but don't play; Rippl says so. WebM isn't supported: `ffmpeg -i in.webm -c copy out.opus` turns WebM with Opus audio into a file Rippl plays.
- AAC, Opus, 32-bit FLAC and ALAC, and FLAC and ALAC above 96 kHz run the second processor at full speed, which may cost some battery.

Playback is gapless. MP3 needs the LAME tag that LAME and ffmpeg write; without one a short gap remains.

## Now Playing

Now Playing shows the cover, title, artist and album, the format and the progress. Hold ▶ to seek (vol± move, ▶ jumps there, ↩ cancels). Playback resumes, paused, where it left off after power-off.

The face comes with the theme. The built-in themes show the standard face in their colours; three themes for the card draw their own ([themes](themes.md#face-themes)):

| Theme | Looks like |
|---|---|
| **Cassette** | A tape deck whose tape winds across the whole album or playlist; the tape at the head is the song's progress |
| **Turntable** | A record at 16⅔ RPM with the cover as its label, and a tonearm that crosses the whole album or playlist |
| **Tape Window** | The player's own window onto a cassette, drawn at a Walkman's scale, with the song on the label and the hubs turning |

Faces fill the whole screen in their own colours and move only while a track plays. With Shuffle songs the album progress stays at the start; with Shuffle albums it crosses each album.

## Queue and modes

- **Shuffle:** Off, Songs or Albums. **Repeat:** Off, One or All. Both are at the top of Settings.
- **Up Next:** hold ▶ on a song for **Play Next** or **Add to Up Next**; ≡ on Now Playing opens the queue. It holds up to 100 songs and survives power-off and rescans.
- **Sleep timer** is at the top of Settings.

## Lyrics

Put a `.lrc` file next to a song with the same name (`01 Come Together.flac` and `01 Come Together.lrc`). Now Playing on the Standard face shows it: in landscape in place of the artist and album lines, in portrait in place of the cover.

- Synced lyrics (`[mm:ss.xx]` lines) scroll with the song, with the current line in bold. Lines with several times and `[offset:]` work; word times are skipped.
- Plain lyrics move down the song as it plays.
- The file must be UTF-8. At most 3 KB of text and 160 lines are read. Lyrics inside music files aren't read.
- Settings → Display → **Lyrics** turns them off.

## Equaliser

The equaliser plays presets from files. Put up to 100 `.txt` presets in an `EQ` folder on the card, in Equalizer APO's parametric format; AutoEQ's `ParametricEQ.txt` works as it is (a `Preamp:` line and up to ten `PK`, `LSC` or `HSC` filters, gain ±12 dB, Q 0.1–10).

Open it from Settings → Equaliser or by holding ≡ on Now Playing. Moving through the list plays each preset; ▶ or ↩ keeps the one you're on. A file the player can't use stays in the list and says why. With no presets on the card, Settings has no Equaliser row.

### Fitted presets

A preset's filters are designed for one sample rate. Played at another rate, the same filters drift near the top of each band, by several dB at worst. The player fits every preset to each rate from 8 to 96 kHz, finding filters for that rate that sound like the preset at its own rate, and plays the fit for each track's rate.

- **Target rate:** 48 kHz, the rate graph.hangout.audio builds its filters at. A preset made at another rate can say so in a comment, for example `# Sample rate: 44100`, which Equalizer APO ignores.
- **When:** after a library scan, and after USB when the music hasn't changed, for each preset without a fit. "Preparing preset 1 of 2..." shows with a progress bar; a preset takes about half a minute. Until then the preset plays with its usual filters.
- **Refit:** hold ▶ on a preset for *Refit preset* or *Refit all presets*. Playback pauses while it runs.
- Tracks at 8–24 kHz are equalised at their own rate before they're raised.
- Fits are kept in `/.rippl/equaliser`; deleting the folder only means the presets are fitted again. [Tide](tide.md) fits them on a computer when it syncs the card, so the player doesn't have to.

The player remembers the preset by file name. It applies to Bluetooth too. It's off for 176.4 kHz and above, for 32-bit tracks and in USB DAC mode. Turning it on mid-track restarts the second processor, so the music pauses for a moment.

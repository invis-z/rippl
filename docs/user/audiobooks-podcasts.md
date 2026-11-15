# Audiobooks and podcasts

Put audiobooks in a folder named `Audiobooks` and podcasts in one named `Podcasts`, at the top of the card (any capitalisation; `Audiobook` and `Podcast` work too). Home then shows **Audiobooks** and **Podcasts**. Their tracks stay out of the Music lists, so books never turn up between your albums.

```
Audiobooks/
  The Long Road/          one folder per book
    01 - Departure.mp3    chapters play in file-name order
    02 - The River.mp3
  Short Story.mp3         a book in one file
  Novel.m4b               an .m4b with chapters
Podcasts/
  Tech Talk/              one folder per show
    2026-10-01 - Episode 41.mp3
```

- **Audiobooks** lists books with "3 chapters", "Chapter 2 of 12" or "Finished". ▶ carries on where you stopped; hold ▶ for the chapter list.
- **Podcasts** lists shows; each episode shows "23:10 left" or "Played".
- Every book and episode keeps its own place, even across music and power-off (`/.rippl/bookmarks.bin`, the last 128).
- They always play in order; your music shuffle setting stays as it was.
- On Now Playing, holding Vol+ jumps 30 s forward and Vol− 15 s back.
- An item counts as finished once its last tenth (at most 30 s) has played.
- An `.m4b` (or `.m4a`) with a chapter list shows "Chapter 3 of 13", and ≡ opens its chapters with their start times. Chapter names aren't shown.
- Only the top-level folders count: the same names deeper down are music. Tracks on internal storage always play as music.

Playback speed can't be changed yet.

`tide bookmarks E: --finished` lists the books and episodes you finished, one path per line, for example to delete played podcasts in a script ([Tide](tide.md)).

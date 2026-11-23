# Theme format

Theme sources are TOML files, packed into `.rippltheme` files by `tools/theme_pack.py`. The packed layout and `theme.bin` are in [file formats](file-formats.md#theme-files). Using themes is in [themes](../user/themes.md).

## Sources and packing

A source names only what it changes. Everything else comes from the base theme, `assets/themes/dark.toml`:

```toml
name = "Rosewood"
author = "Your name"
description = "Deep burgundy with a rounded selection"
selection = "selection"

[colors]
background = "#1c0f14"
selection_top = "#a83250"
selection_background = "#7a2338"

[boxes.selection]
fill = ["selection_top", "selection_background"]
ink = "selection_text"
inset = [4, 2]
radius = 11

[spacing]
row_height = 26
```

```sh
python tools/theme_pack.py --themes my-themes --base assets/themes/dark.toml --definitions src/ui/include/ui --output my-themes/packed
```

This packs every `.toml` in `my-themes` into `my-themes/packed/<file>.rippltheme` and warns about anything hard to read. `--strict` turns warnings into failures.

| Sources | Packed by | Goes |
|---|---|---|
| `assets/themes/*.toml` (built-ins) | `src/themes/packs/` into the firmware image | Built into the player |
| `assets/themes/*.toml` and `.scene` | The `rippl_themes` target | `themes` in the build folder, for the card |

## Limits

| What | Limit |
|---|---|
| Theme head: colours, boxes, accents, icons | 3,072 bytes |
| Wallpaper | 28,224 bytes of player memory |
| One scene, packed | 6,144 bytes |
| Scenes on the player, Home and Now Playing together | About 3,300 bytes |
| Scene steps per drawing | 20,000 |
| Theme files listed on the player | 8 |
| File name, without `.rippltheme` | 1 to 23 bytes |

Typical sizes: 205 bytes with no icons; about 2,000 with every status and list icon; about 1,800 more for six 24 × 24 Home tile icons; about 27,000 for a full-screen wallpaper.

A scene too big for the player doesn't load, and Now Playing or Home shows the standard screen. The packer and the editor warn about it.

## Top level

| Key | Meaning |
|---|---|
| `name` | 1 to 23 bytes. Required |
| `author` | Up to 40 characters |
| `adapted_from` | Optional original author, up to 40 characters |
| `description` | Up to 120 characters |
| `selection` | The box that draws the selected row: `selection`, `selection_flat`, `selection_outline` or any other box |
| `cover_flow` | Cover Flow's stage is always black. `"theme"` (default) puts the theme's text colours on it; `"dark"` the dark look's, for light themes |
| `home_art` | `true` (default) shows album art beside the Home menu, or below it upright; `false` gives the menu the whole screen |

## Colours

`[colors]` sets 16 colours as `"#rrggbb"`. The screen has 16-bit colour, so the low bits are dropped.

| Name | Used for |
|---|---|
| `background` | Lists and plain screens |
| `text`, `text_secondary`, `text_tertiary` | Labels and titles; values and artists; the faintest text and marks |
| `selection_top`, `selection_background`, `selection_shine`, `selection_text` | The selected row's gradient top and bottom, shine line and text |
| `header_top`, `header_background`, `header_text` | The header's gradient and title |
| `divider` | Thin lines and the progress track |
| `scrollbar_thumb` | The scrollbar |
| `danger`, `success` | Low battery, charging |
| `chevron` | The arrow on rows that open something |

These are defaults for the boxes; a box can use any colour for any part.

## Accents

Up to eight `[[accents]]`, listed under Settings → Display → Accent colour. Each replaces the selected row's colours:

```toml
[[accents]]
name = "Ruby"
selection_background = "#900020"
selection_top = "#b00030"
selection_shine = "#e06080"
```

- `name` is 1 to 12 bytes. Blue, Red, Orange, Green, Teal, Purple, Pink and Graphite are translated; other names show as written.
- The first accent is the default.
- Accents never come from the base theme. A theme without any keeps its own selection colours.

## Boxes

`[boxes.<name>]` replaces a box whole. Anything left out is off, and a missing colour is `background`, so give `fill`, and `ink` on boxes with text.

| Box | Draws |
|---|---|
| `selection`, `selection_flat`, `selection_outline` | Selected row choices; only the one `selection` names is drawn |
| `header` | The header |
| `toast` | The short message at the bottom |
| `section` | The letter shown while fast-scrolling |
| `scrollbar_thumb` | The scrollbar's moving part |
| `progress_track`, `progress_fill`, `progress_active` | Now Playing's bar: empty, played, and played while seeking or changing volume |

| Key | Meaning |
|---|---|
| `fill` | A colour, or `[top, bottom]` for a gradient |
| `ink` | Text and arrow colour |
| `border`, `border_width` | Border colour and width in pixels |
| `border_edges` | `"all"` (default with a width) or a list of `"top"`, `"bottom"`, `"left"`, `"right"` |
| `shine`, `shine_height` | A line along the top: colour and height |
| `inset` | `[across, down]` inside the area given |
| `radius` | Corner radius, up to 32, cut to half the shorter side |
| `opacity` | 0 to 100 (default), in 16 steps |

Box colours are names from `[colors]`, not `#rrggbb`.

## Spacing

| Key | Range | Meaning |
|---|---|---|
| `row_height` | 18 to 40 | List row height |
| `padding` | 4 to 20 | Left and right margins |
| `scrollbar_width` | 1 to 8 | Scrollbar width |
| `scrollbar_inset` | 0 to 6 | Scrollbar distance from the edge |
| `detail_gap` | 2 to 16 | Space between a row's label and value |

## Now Playing layout

`[now_playing]` (sideways) and `[now_playing_portrait]` (upright) move, resize, align or hide parts of the standard Now Playing. Each part is its own table:

```toml
[now_playing.cover]
x = 124
y = 30
size = 72

[now_playing.album]
hide = true
```

| Part | What | Default height |
|---|---|---|
| `cover` | The cover; `size` 1 to 170 instead of `width` and `height` | |
| `title`, `artist`, `album` | Text lines | 22 |
| `format` | Codec box, bit depth and sample rate | 16 |
| `detail` | Bit rate and place in the queue | 16 |
| `modes` | Shuffle, repeat and the seek hint | 16 |
| `progress` | The bar, which also shows the volume | 3 |
| `times` | Time played and time left | 16 |
| `lyrics` | Up to five lines, as the height allows | 95 |

- `x` and `y` are from the screen's top-left. The screen is 320 × 170 sideways and 170 × 320 upright; the header is 24 px sideways and 40 px upright.
- `align` is `"left"`, `"center"` or `"right"`. Longer text scrolls.
- Parts left out stay where the player puts them.
- Lyrics hide the cover and any text they overlap, except the title.
- A Now Playing scene replaces this layout.

## Scenes

A scene is a script that draws a whole screen. `[now_playing_scene]` draws Now Playing and `[home_scene]` draws Home:

```toml
[now_playing_scene]
file = "turntable.scene"
whole_screen = true
smooth = true

[now_playing_scene.chrome]
header_text = "#eef1fa"
```

| Key | Meaning |
|---|---|
| `file` or `script` | A script beside the source, or the script inline |
| `chrome` | Header and status bar colours while the scene shows |
| `whole_screen` | Draw over the whole screen, without header and status bar |
| `smooth` | Redraw every 50 ms instead of 100 ms while playing |

On Home the player still owns the menu: the buttons move through it in order and Play opens an item. The `.scene` scripts in `assets/themes` are full examples.

### Language

```
const DEEP = #181418
let cx = width > height ? 84 : width / 2
rect(x, y, width, height, DEEP)
if playing {
    spoke(cx, y + 80, (tick * 3) & 255, 24, 60, 66, #c8eeff)
} else if paused {
    icon(pause, cx - 9, y + 71, 18, 18, #c8eeff)
}
repeat mark from 0 to 48 {
    spoke(cx, y + 80, mark * 16 / 3, 40, 60, 68, mark < progress(48) ? #7fd3ff : #232a42)
}
```

- Numbers are 32-bit integers with C arithmetic. `0x` is hex; `#rrggbb` is a colour.
- `const` is fixed when packed; `let` is worked out while drawing, and `name = ...` changes it. Names end with their `{ }` block.
- Operators, loosest first: `? :`, `||`, `&&`, `|`, `^`, `&`, `== !=`, `< <= > >=`, `<< >> >>>`, `+ -`, `* / %`, prefix `-` and `!`. Comparisons give 1 or 0.
- `if`, `else if`, `else`, and `repeat name from a to b { }` for `a` to `b − 1`.
- `"Quoted"` words are text to show. No separators are needed.

### Inputs

| Name | Meaning |
|---|---|
| `x`, `y`, `width`, `height` | The scene's area: below the header, or the whole screen |
| `playing`, `paused`, `scrubbing`, `volume_shown` | 1 when true |
| `tick` | Tenths of a second into the track, moving only while playing |
| `now` | Milliseconds since start |
| `position` | Milliseconds into the track, smooth while playing |
| `shown`, `duration` | Where the track is (the seek target while seeking) and its length, in ms |
| `volume` | 0 to 32 |
| `badge` | `none`, `play`, `pause`, `next` or `previous` |
| `shuffle_mode`, `repeat_mode` | 0 when off, then the modes in Settings order |
| `track` | Place in the queue |
| `has_cover` | 1 when there is a cover, or album art on Home |
| `flipped` | 1 when upright the other way round |

### Functions

| Function | Gives |
|---|---|
| `min(a, b)`, `max(a, b)`, `abs(a)`, `isqrt(a)` | As named |
| `sin(angle)`, `cos(angle)` | −255 to 255, for 256 steps per turn |
| `polar_x(x, fine, r)`, `polar_y(y, fine, r)` | A point `r` px from (`x`, `y`) at 1,024 steps per turn |
| `progress(w)`, `queue_progress(w)` | How much of `w` the track, or the whole queue, has played |
| `volume_width(w)` | How much of `w` the volume fills |
| `present(text)`, `width(text, font)` | 1 when the text is there; its width in px |
| `segments(digit)` | A digit's seven-segment pattern |
| `items()`, `selected()`, `item(n)` | On Home: the menu's length, the highlighted item, item `n` as text |
| `item_icon(n, x, y, w, h, colour)` | On Home: item `n`'s icon from the theme, centred |

### Drawing

Positions and sizes are pixels. `ring` and the width of `spoke` and `fine_spoke` are in sixteenths of a pixel.

| Command | Draws |
|---|---|
| `rect`, `rounded`, `outline`, `gradient` | Boxes: filled, rounded, a 1 px frame, a top-to-bottom shade |
| `ring(x16, y16, outer16, inner16, colour)` | A disc, or a ring when `inner16` > 0 |
| `spoke(x, y, angle, width16, from, to, colour)` | A bar around (`x`, `y`) at 256 steps per turn |
| `fine_spoke(x, y, fine, width16, from, to, colour)` | The same at 1,024 steps per turn |
| `trapezoid(x, y, w, h, inset, colour)` | A box narrowing toward the bottom |
| `lattice(x, y, w, h, pitch, colour)` | Grid lines |
| `segment_style`, `digit`, `colon` | Seven-segment digits |
| `clip(x, y, w, h)`, `unclip()` | Draw only inside a box, up to 4 deep |
| `cover(x, y, size)`, `round_cover(x, y, radius)` | The cover, square or round |
| `turned_cover(x, y, radius, angle)` | The cover, round and turned, or the Rippl logo label when there is no cover |
| `reflect(x, y, size, rows)` | A fading reflection below the cover |
| `icon(kind, x, y, w, h, colour)` | `play`, `pause`, `next` or `previous` |
| `bar(x, y, w, h, track, fill, hot)` | The progress bar, showing volume in `hot` |
| `backdrop(x, y, w, h)` | The wallpaper, or the background colour |
| `menu(x, y, w, h)` | On Home, the menu as a list |
| `turn(mode)` | Draw a sideways design turned for upright: 1 clockwise, 2 anticlockwise (`flipped ? 2 : 1`) |

Text commands take what to show (`song`, `artist`, `album`, `lyric`, `codec`, `stream`, `detail`, `modes`, `feedback`, `elapsed`, `remaining`, `byline`, `place`, `time_of`, or quoted words), a font (`title`, `body` or `small`) and, where shown, `left`, `center` or `right`:

| Command | Draws |
|---|---|
| `text(what, font, x, y, w, h, align, colour)` | Text cut to fit |
| `line(what, font, x, y, w, h, align, colour)` | Text that scrolls while playing when too wide |
| `plain(what, font, x, y, w, h, colour)` | Text from the left edge |
| `times(font, x, y, w, h, left, right)` | Time played and time left |
| `format(font, x, y, w, h, align, colour)`, `codec(font, x, y, w, h, colour)` | The codec box, with or without the stream |

On Home, the cover is the album art Home shows, and the text sources are the playing track, empty when nothing plays. A script with a mistake draws nothing more; the player shows the standard screen if it can't read the scene.

## Wallpaper

`[wallpaper]` (320 × 170) and `[wallpaper_portrait]` (170 × 320) paint a picture behind the lists in 16 dithered colours:

```toml
[wallpaper]
image = "rosewood.png"
dim = 40
```

- A larger picture is scaled to cover and cut at the middle. A smaller one repeats.
- `dim` fades toward `background`, 0 to 90. Most pictures need 30 to 60.
- With only `[wallpaper]`, upright stays plain unless the picture is at most 170 × 170.
- It can also be written out as `palette` (up to 16 colours) and `rows` (one hex digit per pixel, rows of equal, even length).
- It shows behind lists, menus, Settings, messages and the standard Now Playing.

## Icons

`[icons]` replaces the player's icons. Each is rows of text, one character per pixel: `.` clear, `1` to `e` partly solid, `f` solid. The player colours them:

```toml
[icons]
chevron = [
    ".4....",
    ".d5...",
    "..a8..",
    "...7b.",
    "....d5",
    "...6c.",
    "..98..",
    ".d5...",
    ".4....",
]
```

| Name | Largest | Where |
|---|---|---|
| `status_play`, `status_pause` | 10 × 10 | Status bar |
| `status_bluetooth` | 7 × 10 | Status bar |
| `status_bolt` | 6 × 10 | Before the battery while charging |
| `battery`, `battery_level` | 22 × 10 | The battery frame, and its charge drawn from the left |
| `chevron` | 12 × 18 | End of rows that open something |
| `badge_play`, `badge_pause`, `badge_next`, `badge_previous` | 18 × 18 | Over the cover when a key is pressed |
| `*_large` status and battery icons | 14 to 30 × 14 | The upright status bar; the small ones are used if missing |
| `home_music`, `home_audiobooks`, `home_podcasts`, `home_settings`, `home_diagnostics`, `home_now_playing` | 24 × 24 | `item_icon` in a Home scene |

- Smaller icons are centred. Rows must be the same length.
- `battery` and `battery_level` go together, at the same size.
- An icon takes half a byte per pixel within the 3,072-byte head.

## Readability

The packer and the editor warn when a pair's contrast (1 to 21) is under its floor. For a gradient, the worse end counts.

| Pair | Floor |
|---|---|
| `text` on `background` | 4.5 |
| `text_secondary` on `background` | 3.0 |
| `text_tertiary` on `background` | 2.5 |
| `chevron` on `background` | 2.0 |
| `danger`, `success` on `background` | 3.0 |
| Selected row ink on its fill | 3.0 |
| Header, toast and fast-scroll ink on their fills | 4.5 |
| Progress fill on its track | 2.0 |
| `text` on the wallpaper, without its worst tenth | 3.0 |

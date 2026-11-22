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
| `assets/themes/*.toml` | The firmware build | Into the image, as the built-in themes |

## Limits

| What | Limit |
|---|---|
| Theme head: colours, boxes, accents, icons | 3,072 bytes |
| Wallpapers and fonts together | 28,224 bytes of player memory |
| Theme files listed on the player | 8 |
| File name, without `.rippltheme` | 1 to 23 bytes |

Typical sizes: 205 bytes with no icons; about 2,000 with every status and list icon; about 27,000 for a full-screen wallpaper; about 5,000 for a 16 px font.

## Top level

| Key | Meaning |
|---|---|
| `name` | 1 to 23 bytes. Required |
| `author` | Up to 40 characters |
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

## Fonts

`[fonts.title]`, `[fonts.body]` and `[fonts.small]` replace the player's fonts:

| Font | Used for | Tallest line |
|---|---|---|
| `title` | Header titles | 20 px |
| `body` | Lists, menus, the large status bar | 20 px |
| `small` | Second lines, small print, the status bar | 15 px |

```toml
[fonts.body]
file = "MyFont-Medium.ttf"

[fonts.small]
file = "MyFont-Regular.ttf"
size = 11
```

- Without `size` the packer picks the largest that fits.
- Fonts cover basic Latin, digits, punctuation, curly quotes, dashes, the ellipsis, €, ★ and ✓. Other characters use the player's own font.

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

- Smaller icons are centred. Rows must be the same length.
- `battery` and `battery_level` go together, at the same size.
- An icon takes half a byte per pixel within the 3,072-byte head.

## Readability

The packer warns when a pair's contrast (1 to 21) is under its floor. For a gradient, the worse end counts.

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

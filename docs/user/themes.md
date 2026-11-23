# Themes

A theme sets Rippl's colours, the shape of its rows and bars, its spacing, and optionally a wallpaper, fonts, icons and its own Home and Now Playing screens. The built-in themes are in Settings → Display → Theme. More come as `.rippltheme` files on the card.

## Using a theme file

1. Make a folder called `Themes` at the top of the microSD card.
2. Copy the `.rippltheme` file into it, or let [Tide](tide.md) do it: `tide themes E: install turntable.rippltheme`.
3. On the player, pick it in Settings → Display → Theme. Theme files come after the built-in themes.

- The list shows up to 8 theme files, each under its file name. Rename the file to rename the theme.
- A name may be 1 to 23 bytes long. Longer names are skipped.
- Accent colour lists the theme's own accents, and is hidden when it has none.
- If the file is deleted or damaged, the player goes back to the built-in theme used last.
- Keep the file in `Themes` while it's worn: after USB or a rescan the player reads its wallpaper and fonts from it again.

`tide themes E:` lists the themes on the card, and `tide themes E: remove NAME` removes one.

## Face themes

Cassette, Turntable and Tape Window are theme files that draw their own Now Playing screen, over the whole screen. They aren't in the image: the build packs them into `build/dev/themes`. Copy the ones you want into `Themes`. Any other theme shows the standard Now Playing.

## Making a theme

The theme editor in `web/editor` runs the player's own interface in a browser, so its screen is what the player draws. It isn't hosted; [building](../development/building.md#theme-editor) explains how to build and open it.

- **Colours:** Background, Text and Accent set the whole theme. Colour sets and Colours from a picture recolour everything at once, and a Fix button appears where text is hard to read.
- **Look:** rows, the title bar, spacing, a wallpaper, fonts and icons.
- **Screens:** Make it my own turns Home or Now Playing into a drawing of parts you place: text, the cover, boxes, circles, lines, the progress bar, the menu as a list or tiles. Parts can move with the music.
- **Upright:** a screen drawn sideways is arranged upright for you. Move anything upright and that layout becomes yours.
- **History:** My themes keeps your themes in the browser, with earlier versions of the open one.

Themes can also be written by hand as TOML and packed with `tools/theme_pack.py`. The format is in the [theme format reference](../reference/theme-format.md).

## Sharing a theme

The `.rippltheme` file is the whole theme. Send it to anyone; they copy it into `Themes` on their card.

The editor opens a `.rippltheme` file too, so anyone can start from yours. **Save an editable copy** also writes the theme as a `.toml` source, with its wallpaper and fonts written out, for changing by hand and packing with `tools/theme_pack.py`.

Only include a font whose licence allows passing it on, such as the SIL Open Font License. The editor asks where a theme's fonts are from.

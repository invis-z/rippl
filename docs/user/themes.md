# Themes

A theme sets Rippl's colours, the shape of its rows and bars, its spacing, and optionally a wallpaper, fonts and icons. The built-in themes are in Settings → Display → Theme. More come as `.rippltheme` files on the card.

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

## Making a theme

A theme is written as TOML and packed with `tools/theme_pack.py`. The format is in the [theme format reference](../reference/theme-format.md).

## Sharing a theme

The `.rippltheme` file is the whole theme. Send it to anyone; they copy it into `Themes` on their card.

To let others change it, send its `.toml` source too.

Only include a font whose licence allows passing it on, such as the SIL Open Font License.

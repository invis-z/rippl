# Troubleshooting

What to do when something doesn't work, and what to include in a report.

## A file doesn't play or load

`/.rippl/errors.log` on the card lists each file the player couldn't open or use, one line each: the time, what went wrong ("can't play this format", "path too long", "can't use the preset (line 4)") and the path. Files that are allowed to be missing, such as lyrics and cover images, aren't listed. A session writes at most 64 lines. The log is never cleared; delete it to empty it.

## The install doesn't take

If "Updating" flashes for under a second and FiiO's firmware comes back:

1. Copy the file again and check its hash on the player's drive.
2. Make sure it's the only `HIFIECnn.IMG` at the top level.

If it keeps failing, the only other way in is maskrom ([recovery](../development/recovery.md)).

## Going back to FiiO is refused

"isn't a valid image for this player": check that you copied FiiO's `HIFIECnn.IMG`, not `MINIVnnn.IMG`, and that it's unchanged.

## The player doesn't start

- **After an update:** press the RST hole once and start it. Then photograph Diagnostics → Last update, Last start and Boot hold.
- **Not at all:** the chip's maskrom mode can write FiiO's firmware back over USB ([recovery](../development/recovery.md)).

## A STALL or FAULT screen

White text on black with PC and LR lines: photograph it, then press RST rather than holding Power. RST keeps the player's memory, so Diagnostics → Last start and Last stack still show what stopped; holding Power on battery wipes it.

## The screen looks wrong

Diagnostics → **Screen output** switches how the screen is sent: VOP DMA (the default), VOP, or GPIO. It isn't saved, so a restart always returns to the default.

## Reporting a problem

Include what you did, your FiiO version before installing, and these Diagnostics lines: Firmware, Last start, Last step, Last stack, Last USB, USB errors and Last update. A photo of the screen helps.

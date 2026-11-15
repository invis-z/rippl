# Installing Rippl

How to put Rippl on an Echo Mini, keep it up to date, and go back to FiiO's firmware.

> **Use it at your own risk.** Rippl comes with no warranty (see [LICENSE](../../LICENSE)). Replacing a player's firmware can leave it unable to start; during development that happened, and the player was recovered through maskrom ([recovery](../development/recovery.md)). Read [known issues](known-issues.md) first.

## Supported players

| Player | FiiO update file | Supported |
|---|---|---|
| Echo Mini 8 GB | `HIFIECnn.IMG` | Yes |
| Echo Mini 512 MB | `MINIVnnn.IMG` | No: its firmware ignores Rippl's file, and Rippl refuses its firmware |

Settings → About on the player shows its FiiO version.

## Firmware files

A build produces `RIPPL.IMG`, the 8 MiB firmware image. Rippl's own updater installs it.

Releases don't carry the firmware yet; until they do, build it ([building](../development/building.md)). Check a downloaded file against its `SHA256SUMS.txt` line before installing:

```sh
shasum -a 256 RIPPL.IMG          # macOS, Linux
Get-FileHash RIPPL.IMG           # Windows PowerShell
```

## Before you install

1. Copy anything you want to keep off the player's internal storage.
2. Charge the battery.
3. Delete any `HIFIECnn.IMG` or `ECHOMINI.IMG` file at the top level of internal storage: an updater installs the one with the highest number.

## Install from FiiO's firmware

> **Untested.** FiiO's updater hasn't been tried with Rippl's 8 MiB image.

1. Connect the player. Its internal storage appears as a drive (named ECHO MINI unless you renamed it).
2. Rename `RIPPL.IMG` to `HIFIEC90.IMG` and copy it to the top level of that drive, not into a folder.
3. Eject the drive, unplug, and take the microSD card out.
4. Switch the player off and on again. FiiO's firmware shows "Updating, Don't power off"; leave it alone until Rippl starts.
5. Put the microSD card back. The first start with a card builds the library and covers.

If "Updating" flashes for under a second and FiiO's firmware comes back, the install didn't take: see [troubleshooting](troubleshooting.md#the-install-doesnt-take).

## From EchoPod

Rippl can't be installed from EchoPod: EchoPod (up to 0.12.0) accepts only 5 MiB images, and Rippl's is 8 MiB.

## Update Rippl

1. Copy the new `RIPPL.IMG` to the top level of internal storage.
2. Eject both drives. The player shows "Checking RIPPL.IMG", then "Installing update" with a percentage, then "Update installed".
3. Unplug. The player restarts into the new version.

The updater checks the image before writing, skips an image that is already installed, writes one firmware slot at a time and reads every chunk back. It looks for `RIPPL.IMG` first, then `ECHOMINI.IMG`, then `HIFIEC97.IMG` down to `HIFIEC00.IMG`. Leave no `HIFIECnn.IMG` on the drive afterwards, so FiiO's firmware won't reinstall it if you ever go back. `RIPPL.IMG` can stay.

## Go back to FiiO's firmware

1. Download FiiO's firmware for the **8 GB** Echo Mini from FiiO's website and unzip it.
2. Delete `RIPPL.IMG` and any `HIFIEC90.IMG` from internal storage.
3. Copy FiiO's `HIFIECnn.IMG` (for example `HIFIEC40.IMG`) to the top level of internal storage, unchanged. Don't use the `MINIVnnn.IMG` from the same zip: that one is for the 512 MB model.
4. Eject both drives. The player checks the file and shows "Installing FiiO firmware", then "FiiO firmware installed".
5. Take the microSD card out, then unplug. The player restarts into FiiO's firmware, which shows "Updating" for a minute or two while it installs the same file, and deletes it.

To switch to another custom firmware, go back to FiiO's first: Rippl only installs its own images and FiiO's.

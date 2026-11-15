# Maskrom recovery

When a player doesn't start at all, the chip's maskrom mode can still write FiiO's firmware back over USB. This is the last resort; the normal way back is a file copy ([install](../user/install.md#go-back-to-fiios-firmware)).

Maskrom writes go straight to internal storage, below the file system, and a wrong command can leave the player unable to start until a later write succeeds. Follow the steps exactly and run nothing else. The procedure follows ReChord's Linux debrick guide and the livetrack.club guide.

## What you need

- FiiO's firmware for the **8 GB** model, from FiiO's Echo Mini firmware page.
- A data USB cable, plugged into a port on the computer itself, not a hub.
- A paperclip for the RST hole.
- The patched `rkdeveloptool` (below). Never use another build: the upstream tool doesn't know this device.

Internal storage may be wiped. If the player still shows its drive, copy off what you want to keep first.

## 1. Build the patched rkdeveloptool (once)

The upstream tool needs three changes for the Echo Mini: a 5000 ms command timeout, 32 sectors per transfer, and maskrom detection for USB ID `2207:262d`. `tools/patch_rkdeveloptool.py` makes them, prints `ok: Echo Mini changes applied`, and refuses a source it doesn't recognise or one already patched.

### Windows

In an MSYS2 CLANG64 shell with the packages from [building](building.md#windows) plus `mingw-w64-clang-x86_64-autotools`, `mingw-w64-clang-x86_64-libusb` and `git`, from the repository's folder:

```sh
mkdir -p external && cd external
git clone https://github.com/rockchip-linux/rkdeveloptool.git
cd rkdeveloptool
git checkout 304f073752fd25c854e1bcf05d8e7f925b1f4e14
python ../../tools/patch_rkdeveloptool.py .
autoreconf -fi
mkdir -p build-clang64 && cd build-clang64
../configure --prefix=/clang64
make AM_CPPFLAGS='-Wall -Wextra -Wreturn-type -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_LARGE_FILE $(LIBUSB1_CFLAGS)'
./rkdeveloptool -v
```

The `make` line drops upstream's `-Werror`, as MSYS2's own package does. The tool prints `rkdeveloptool ver 1.32` and needs `C:\msys64\clang64\bin` on the PATH.

### macOS

```sh
brew install libusb automake autoconf pkgconf
mkdir -p external && cd external
git clone https://github.com/rockchip-linux/rkdeveloptool.git
cd rkdeveloptool
git checkout 304f073752fd25c854e1bcf05d8e7f925b1f4e14
python3 ../../tools/patch_rkdeveloptool.py .
./autogen.sh && ./configure && make CXXFLAGS="-g -O2 -Wno-vla-cxx-extension"
./rkdeveloptool -v
```

Apple clang rejects two run-time-sized arrays in upstream `main.cpp` without that flag.

## 2. Enter maskrom

1. Unplug USB and press the **RST** hole so the player is fully off. Don't use Power.
2. Hold **every button except Power**: ▶‖, ≡, ↩ and the volume rocker.
3. Still holding, plug the cable in. Let go after 2–3 seconds.
4. The screen stays black. That's expected.

**Windows only, once per computer:** Windows lists `USB\VID_2207&PID_262D` without a driver. Run [Zadig](https://github.com/pbatard/libwdi/releases), choose Options → List All Devices, pick the entry whose USB ID reads `2207` `262D` (check both numbers: replacing another device's driver breaks it), set WinUSB and click Install Driver. Only maskrom uses this ID.

## 3. Check the connection

```sh
./rkdeveloptool ld
./rkdeveloptool td
```

Continue only if `ld` shows `Maskrom` and `td` says `Test Device OK.`

## 4. Write FiiO's firmware

1. Check the image. For FiiO 3.7.0 (`HIFIEC37.IMG`), SHA-256 must be `eba715acbc55875fd3be4b9b1f5f58c777bca130049320043ffc09bc2866b228`.
2. Write it and restart:

   ```sh
   ./rkdeveloptool wl 0 HIFIEC37.IMG
   ./rkdeveloptool rd
   ```

   `wl` writes 32 MiB and prints about 2,000 progress lines up to 100 %. Don't unplug or let the computer sleep until it finishes. `rd` prints `Reset Device OK.`
3. The player starts FiiO's firmware. If it stays black, unplug, press RST, then press Power. Settings → About should show the version you wrote.

If you can still see the drive, remove any `HIFIECnn.IMG` or `ECHOMINI.IMG` from it first, or FiiO's firmware reinstalls it at its first start.

## Rules

- Never run `db`: it fails on this boot ROM and can wedge USB. Never write anything but the whole FiiO image: no `ul`, `ef`, `wlx`, unpacked parts or RKAF tools.
- If a command fails or hangs for more than about a minute, press Ctrl+C, unplug, press RST, enter maskrom again and start over from `ld`.
- If `wl` fails twice the same way, try another port, cable or computer. Linux is the fallback ReChord used.

## If it goes wrong

| Symptom | Try |
|---|---|
| Nothing with `2207:262d` appears | Repeat step 2 with a data cable in a port on the computer |
| `ld` says `Loader`, not `Maskrom` | The patch didn't apply; rebuild the tool |
| `ld` lists nothing on Windows | Bind the device to WinUSB with Zadig |
| `td` hangs or times out | Unplug, press RST and start again; on Linux, disable USB autosuspend |
| The player won't start after `rd` | Press RST, wait 10 s, then press Power |

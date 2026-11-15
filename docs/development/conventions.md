# Contributing

Thanks for helping with Rippl. This page covers how the code is written, tested and committed.

## Getting set up

Follow [building](building.md) to build the simulator and firmware and run the tests. Test changes on a player only if you can [recover it](recovery.md); [device testing](device-testing.md) covers installing builds.

## Code style

- **C11**, formatted by `clang-format` with the repository's `.clang-format` (LLVM base, 4-space indent, 100 columns, braces on every block). The `format` test fails on unformatted files.
- **No comments** in C, CMake or Python: names and small functions carry the meaning, and commit messages carry the why. The `no_comments` test enforces it; third-party code is exempt.
- **Static memory.** Rippl's own code doesn't use the heap; memory is static and budgeted. The `target_audit` test fails if core 0's static RAM grows past its budget, and the firmware build fails if code or an overlay outgrows its region. Check the sizes the build prints.
- Keep logic that can run on a computer out of `platform/device/board`, so the host tests can reach it.

## Tests

`ctest --preset dev` must pass before a change is committed. Add host tests for new behaviour; app-level tests drive the whole UI through a fake HAL (`tests/app`).

Generated files are checked into the repository and verified by `*_up_to_date` tests: language packs, glyph packs, fonts, test fixtures and tables. When one fails, run the tool it names and commit the result with the change that needed it.

## Translations

UI strings live in `src/core/include/core/strings_*.def` (English) and `assets/lang/<code>.txt` (one `STR_NAME = text` line per string). After changing them, rebuild the packs:

```sh
python3 tools/lang_pack.py --catalogue src/core/include/core --languages assets/lang --output src/lang/packs
python3 tools/glyph_pack.py --config assets/fonts/glyph_pack.json --output src/fonts/packs
```

## Commits

Each commit is one complete, working change: it builds, passes the tests, and updates the documentation it affects.

```
Take USB DAC audio packets in an interrupt

Packets were read only when the main loop polled the controller, so a
long redraw could leave the endpoint unarmed and drop audio.

- IRQ 5 feeds the DAC session's receive routine directly.
- Changing the stream format re-arms the endpoint.
```

- **Subject:** imperative mood, at most 60 characters, no trailing period, describing the effect ("Play 384 kHz FLAC", not "Changes to flac.c").
- **Body:** wrapped at 72 columns. Say what changed and why, and what a reviewer needs to know: limits, trade-offs, measured results that justify the change. Use bullets for separate parts.
- Leave out how the change was found or iterated on; that belongs in a pull request.

## Documentation

- [User docs](../user) describe what the player does, for people using it. [Development docs](.) and the [reference](../reference) are for people working on it.
- Write plainly: short sentences, present tense, tables for lists of facts. State what's tested and what isn't.
- Update the [changelog](../../CHANGELOG.md) for anything a user would notice.

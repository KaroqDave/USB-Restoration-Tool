# Tasks

## Next

- On Windows, run `./scripts/build-standalone.ps1` with `pwsh` from an unelevated
  shell. CI cannot cover this: its runners are elevated.

## Recently completed

- 2026-11-04: Removed duplicated and dead code across `src/`. The two GPT type
  GUIDs use `QByteArray::fromHex`; the filesystem name, label and token come from
  one table; `busTypeName` is a table; the signature-clear range moved to
  `signatureClearRange()` in `src/core/safety.*` with a test, so both backends
  clear provably the same bytes; `effectiveSectorSize()`,
  `deviceNumbersOnDisk()`, `formatByteSizeMessage()` and `isKnownFileSystem()`
  replace repeated copies; the protocol flag names are one set of constants; and
  79 redundant `if (error || displayError)` guards are gone. Removed the unused
  `DiskInfo::health` chain, `StatusKind::Success`, a `QLineEdit` stylesheet block,
  two `windowsErrorMessage` overloads and four unused includes. `docs/TODO.md`
  went from 430 lines to this note, the README keeps only 1.9.0 and 1.8.0 notes,
  and `AGENTS.md` links to `docs/development.md` instead of repeating it. Core
  (174), GUI (139, 2 skipped), Linux (11, 29 loop cases skipped) and all 27 fuzz
  seeds pass; the loop cases need root, which this host would not grant. Windows
  sources were not compiled here. No physical drive accessed.
- 2026-11-04: Review fixes for the same branch. The filesystem labels are marked
  with `QT_TRANSLATE_NOOP` again, so `update_translations` keeps their German text;
  `signatureClearRange()` now also returns the tail offset and the sector size both
  backends write with; enumeration uses `deviceNumbersOnDisk()`; WMI Format gets
  its own filesystem names; `initializeComSecurity()` reports its error again and
  `main.cpp` logs it; the helper's display text again requires an accepted
  version; and the GUI tests' `find<T>()` fails the current test instead of
  asserting. All five ctest suites pass. Windows sources still not compiled.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Keep completion notes
here only while a task is still in flight.

Design boundaries and intentional exclusions are in [polkit-helper.md](polkit-helper.md)
and [SECURITY.md](../SECURITY.md).

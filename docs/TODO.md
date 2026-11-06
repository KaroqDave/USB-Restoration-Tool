# Tasks

## Next

- On Windows, run `./scripts/build-standalone.ps1` with `pwsh` from an unelevated
  shell. CI cannot cover this: its runners are elevated.
- On Windows, configure, build and test with the `windows` preset from a local
  Developer PowerShell. CI's `build-windows-preset` job covers the same steps.

### 1.9.5 release

PRs #13, #14 and #15 are merged into `main`. To ship: tag `v1.9.5` on `main`.

## Recently completed

- 2026-11-06: Re-dated the 1.9.5 changelog heading and merged #13, #14 and #15
  into `main` in order. The Linux loop tests passed as root on 2026-11-05, and a
  restore on a disposable USB drive succeeded.

- 2026-11-05: Item 23, the disk scan, runs on a `QThread` (PR #14). Restore,
  Refresh and both drop-downs stay disabled while it runs, a second Refresh is
  ignored, and the window waits for the scan before it is destroyed. The GUI
  tests wait with `waitForScan()`; `keepsRestoreAndRefreshDisabledWhileScanning`
  pins the behaviour. One new German string. Release commit: version 1.9.5,
  README and changelog notes, and regenerated screenshots.

- 2026-11-05: 1.9.5 cleanup items 1–22 and 24 on PR #13, one commit per item or
  tight pair. Step counts derive from `restorePlan()`; the English-only Windows
  wrappers, leftover guards and helper copies are gone; `restore_steps`,
  `loop_device`, `admin` and `disk_enumerator` merged into neighbours (66 → 58
  files); Windows volume checks share `volumePathIsOnDisk()` and a scan opens
  each drive letter once; one `findSystemExecutable()`; a formatter table; one
  block-device number check; a header-button helper; `src/core` builds once as
  a hardened library (plus a sanitized copy for sanitizer builds);
  `usbrestore_configure()` replaces per-target settings; translations take
  their sources from the source variables (405 strings, unchanged); the
  `windows` preset uses Ninja Multi-Config and the database synthesis is gone
  (`CMakeLists.txt` 889 → 704 lines); mountinfo is read once per Linux scan;
  Fusion is set once; FAT32 limits read "2.00 TB"; WMI uses `ComPtr`. Linux
  ctest, the sanitizer/libFuzzer build and lupdate pass here; Windows only
  through CI. Loop tests not run (need root). No physical drive accessed.

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
- 2026-11-04: Reviewed the whole branch diff hunk by hunk. The table, GUID and
  flag-constant rewrites match the code they replace, every removed
  `if (error || displayError)` guard was redundant, and no test assertion was
  lost. Restored the per-module and per-test detail that the `AGENTS.md` trim
  had dropped, in the project structure table of `docs/development.md`.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Keep completion notes
here only while a task is still in flight.

Design boundaries and intentional exclusions are in [polkit-helper.md](polkit-helper.md)
and [SECURITY.md](../SECURITY.md).

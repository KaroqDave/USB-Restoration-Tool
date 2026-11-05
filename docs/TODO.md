# Tasks

## Next

- On Windows, run `./scripts/build-standalone.ps1` with `pwsh` from an unelevated
  shell. CI cannot cover this: its runners are elevated.

### 1.9.5: source cleanup (from the 2026-11-04 full source review)

Items 1–22 and 24 go on the PR #13 branch as separate commits, pushed phase by
phase so Windows CI sees each Windows change early. Item 23 gets its own PR. Then
a release commit bumps `PROJECT_VERSION` to 1.9.5, adds the README
`## What's New in 1.9.5` section and the changelog entry, and `v1.9.5` is tagged.
Items marked (Windows) are only compiled by CI. No item changes a safety rule;
21 and 23 change user-visible text.

Order: 1–5; 11–14 as pure moves; 6 with 19, then 7–10; 15, 17, 16, 22; 18, 20;
21, 24; then 23.

Decisions: 15 builds core twice, hardened for the app and helper and sanitized for
the tests, and core must stay in the `qt_add_translations` source targets; 22 moves
the `windows` preset to Ninja Multi-Config and keeps the Linux link and
inactive-platform skip; 24 uses `Microsoft::WRL::ComPtr`, since `_com_ptr_t`
throws; 21 uses `formatByteSize`; 23 scans on a `QThread`, drops stale results,
and keeps Refresh and Restore disabled while scanning.

Remove:

1. Make `DiskService::totalRestoreSteps()` and `firstDestructiveStep()` non-virtual
   helpers derived from `restorePlan()`. Linux, Windows and the GUI test fake all
   compute them that way already.
2. Drop the English-only `windowsDeviceErrorText()` and `windowsRestoreErrorText()`
   wrappers; `test_wmi.cpp` uses `.english` instead. (Windows)
3. Remove the leftover `if (reason || displayReason)` guards in
   `canCreateFileSystemOn` and `WindowsDiskService::canFormatFileSystem`.
4. In `performLinuxRestore`, pass `error`/`displayError` straight to
   `checkRestoreError()` instead of copying through `reason`/`displayReason` (5 places).
5. Leftover copies: `effectiveSectorSize()` in `detectPartitionStyle`;
   `boundedEntryCount()` in `volumeHasDiskExtent` (Windows); the doubled `errno`
   save in `LoopDevice::configure`; unused `<cstring>` in `partition_table.cpp`;
   make `containsProtectedMountPoint()` internal to `safety.cpp`.

Deduplicate:

6. One helper for "open the volume, check it sits on this disk, close it", now
   written six times in `volume_manager.cpp` and `disk_enumerator.cpp` (one copy
   re-implements `openVolumePath`); share the two A–Z drive-letter loops. (Windows)
7. Merge `findSystemTool` (`linux_restore.cpp`) and `findPkexec`
   (`helper_client.cpp`) into one fixed-path executable lookup in `sysfs.cpp`.
8. Turn `resolveFormatTool`'s four "try `mkfs.X`, then the alternate name"
   blocks into a table.
9. Share the "is a block device and its numbers match" code between
   `BlockDevice::verifyIdentity` and `verifyPartitionOf`.
10. A helper for the four header text buttons in `main_window.cpp`.

Fewer files (66 → 58):

11. Merge `linux/loop_device.*` into `linux/block_device.*`.
12. Merge `core/restore_steps.*` into `core/restore_message.*`.
13. Merge `win/admin.*` into `win/windows_util.*`. (Windows)
14. Merge `win/disk_enumerator.*` into `win/windows_disk_service.*`. (Windows)

Build (`CMakeLists.txt`):

15. Build `src/core` once as a static library instead of in seven targets; the
    fuzzer keeps its own copy for libFuzzer instrumentation. Mind which flags the
    library gets: the app and helper are hardened, the tests sanitized.
16. One function for the repeated include-dir, `USBRESTORE_APP_VERSION`,
    `UNICODE`/`NOMINMAX` and `/utf-8` settings (about six copies).
17. Build the `qt_add_translations` source list from the source variables
    instead of a hand-kept list.

Performance:

18. Linux scan: read `/proc/self/mountinfo` once per scan, not once per USB disk.
19. Windows scan: map drive letters to disks in one pass instead of opening A–Z
    once per USB disk. (Windows)
20. `applyTheme()`: set the Fusion style once at startup instead of on every theme
    change, which restyles every widget twice.

Behaviour and tooling:

21. The two FAT32 size refusals use `QLocale::formattedDataSize` ("4 TiB") where
    every other message uses `formatByteSize` ("4.00 TB"); switch them.
22. Remove the compile-database synthesis from `CMakeLists.txt` and move the
    `windows` preset to Ninja Multi-Config, so clangd reads the real database.
23. Run the disk scan off the GUI thread; WMI can freeze the window for a second
    at startup and on Refresh. The GUI tests assume a synchronous scan. Own PR.
24. Replace the manual COM reference counting in `WmiObject` and
    `WmiConnection::callMethod` with `ComPtr`. (Windows)

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
- 2026-11-04: Reviewed the whole branch diff hunk by hunk. The table, GUID and
  flag-constant rewrites match the code they replace, every removed
  `if (error || displayError)` guard was redundant, and no test assertion was
  lost. Restored the per-module and per-test detail that the `AGENTS.md` trim
  had dropped, in the project structure table of `docs/development.md`.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Keep completion notes
here only while a task is still in flight.

Design boundaries and intentional exclusions are in [polkit-helper.md](polkit-helper.md)
and [SECURITY.md](../SECURITY.md).

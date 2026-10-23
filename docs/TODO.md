# Tasks

## Next

Findings from the 2026-10-23 review of v1.7.0..v1.7.1, highest priority first.

- Windows: `waitForStorageJob` now fails when the job object has vanished or a
  completed job has no `ErrorCode`, which can report failure after the disk was
  wiped. Confirm on hardware how Windows removes jobs and reports `ErrorCode`, then
  decide whether a vanished or code-less completed job is success (`src/win/wmi.cpp`).

## Recently completed

- 2026-10-24: Closed the remaining v1.7.1 review cleanups. A busy loop node no
  longer stalls the formatter mapping: after `EBUSY` it asks loop-control for a
  new node, and a failed `configure` leaves no path behind.
  `BlockDevice::verifyIdentity` drops its redundant sequence check. The preflight,
  table writer and Windows `RawDisk` share `partitionLayoutError`. The sequence
  refusal names the disk, the `isSameRestoreTarget` comments describe the
  sequence rule, and AGENTS.md lists all three test files. Windows Release build
  and core tests (155) pass; this machine's app-control policy blocked the new
  WMI test binary, and the Linux tests run only in CI. No physical-drive restore
  was performed.

- 2026-10-24: Fixed three Linux restore regressions from 1.7.1. A formatter
  failure now still re-reads the partition table, the new partition is checked
  against the disk sequence read after the re-read, and the read-only identity
  handle is closed before unmounting. `formatAndRereadPartitionTable` holds the
  Format and re-read steps; `rereadsPartitionTableAfterFormatting` runs them on a
  partition-scanning loop image at both sector sizes, with mkfs failing and
  succeeding. Formatting passes locally; the Linux build and image tests run
  only in CI. No physical-drive restore was performed.

- 2026-10-23: Published 1.7.1 as the latest stable release after all five release
  jobs passed. Verified the downloaded Windows ZIP, AppImage, Debian package and
  both checksum files against GitHub's asset digests, both Linux checksums, all
  20 published Windows bundle files and the executable's 1.7.1 version. Confirmed
  the ZIP instructions and release notes match the release preparation. The
  obsolete review branch has been removed locally and on GitHub.

- 2026-10-23: Prepared 1.7.1 with restore-safety release notes and the Linux
  kernel/loop-driver requirement. Updated the Windows ZIP instructions to match
  the erase checkbox and partition/filesystem choices. Windows Release build,
  all three test suites and the release preflight pass. Verified the local ZIP's
  executable version and all 22 bundled file checksums. Deleted the fully merged
  review branch locally and on GitHub; platform release checks run in CI.

- 2026-10-22: Added Linux mapping cleanup regressions for cancellation before
  erasure, formatter failure and launch failure at both sector sizes. Enabled
  ASan/UBSan for the Linux test target and its privileged image tests in CI;
  corrected the GUI refusal message for unsupported partition layouts.
  Windows Release build, all three local test suites, formatting and diff checks
  pass. Debian image tests pass with 25 tests and no skips, including all six
  cleanup cases; the image suite also passes under ASan/UBSan in CI. No
  physical-drive restore was performed.

- 2026-10-22: Implemented the four full-review fixes: shared layout preflight
  before erasure, Linux disk-sequence identity checks, formatter mappings backed
  by the verified exclusive disk handle, and fail-closed WMI job monitoring.
  Added core, Windows preflight/WMI and Linux image regression tests; updated
  helper protocol to 4 and documented the Linux 5.15+/loop-driver requirement.
  PR #8 CI passes on commit `2785cc0`: Windows tests, Linux build and partition-table
  verification, Debian packaging, and sanitizer tests/fuzzing. All 16 image-only
  formatter cases pass across four filesystems, two sector sizes and both file
  and exclusive block-device backing. CI resolves the local validation gaps;
  no physical-drive restore was performed.

- 2026-10-22: Published 1.7.0 as the latest stable release after all four CI
  jobs and all five release jobs passed. Verified the downloaded Windows ZIP,
  Linux AppImage and Debian package against the published hashes and checksum
  files, all 20 Windows bundle files, and the executable's 1.7.0 version.

- 2026-10-22: Prepared 1.7.0 with release notes, changelog, current support
  policy and updated interface previews. Windows Release build, all three test
  suites and the release workflow's version/notes preflight pass.

- 2026-10-22: Made the erase acknowledgement clearly visible with a larger
  outlined checkbox, contrasting checkmark and keyboard focus. Checked both
  themes at 100% and 200% scaling, including Space toggling the erase gate.
  Windows Release build passes.

- 2026-10-22: Reproduced the FAT32 `AllocationUnitSize` type mismatch in an
  in-memory WMI object and corrected the numeric COM encoding. Added Windows
  regression tests that run without a storage provider or disk access. Release
  build, core tests, WMI tests and fuzz-corpus replay pass. GPT/FAT32 restore
  confirmed working by the user.

- 2026-10-22: Refreshed the branding with a scalable USB restore icon, nine
  Windows icon sizes, mint and ink themes, a two-column interface and README
  artwork. Windows Release build, core tests and fuzz-corpus replay pass;
  rendered UI checks cover both themes, compact windows, 200% scaling and the
  erase acknowledgement. No physical-drive restore was performed.

- 2026-10-22: Confirmed the 1.6.3 release: all five workflow jobs passed, and
  GitHub lists it as the latest stable release with the Windows ZIP, Linux
  AppImage and Debian package, plus both Linux checksum files.

- 2026-10-22: Prepared 1.6.3: bumped the project version, updated the README
  release notes and changelog. Windows Release build, core tests, fuzz-corpus
  replay and release preflight pass. Built the Windows portable ZIP and
  verified its executable version and every bundled file against the checksums.

- 2026-10-21: Windows restores a "superfloppy" stick (bootloader on the bare
  disk, no partition table), which failed with `DeleteObject` return value 1
  and then error 21 on the first write. Windows fakes a whole-disk partition
  with an offline volume that blocks `WriteFile`. Every restore now writes an
  empty GPT through the raw handle after the WMI deletes and waits for the old
  volumes to leave the disk. A RAW reset was not enough, since it keeps the
  boot code's jump instruction. With the final code, three cases restored to
  GPT exFAT: the affected stick returned to its original sector 0 with the
  volume offline, the same superfloppy with its volume online at a drive
  letter, and an ordinary stick with a mounted partition. Release build, core
  tests and CI pass. Ships in 1.6.3.

- 2026-10-21: Hardened the helper's trust boundary. Argument sizes accept plain
  digits only, a repeated version detail or second announcement is refused, and
  protocol text stays within its limit without splitting a surrogate pair.
  `usb-core-fuzz` fuzzes the parsers and partition tables and replays
  `tests/fuzz-corpus` in ctest; the `sanitizers` CI job runs the core tests
  and partition-table checks under ASan and UBSan and fuzzes for two minutes.
  CI passes on all four jobs. Ships in 1.6.2.

- 2026-10-18: Step plans for both backends moved to `src/core/restore_steps.*`
  with tests pinning the step count and first destructive step. The helper
  announces its plan and app version (protocol 3), and the GUI refuses a
  mismatched helper. CI passes on Windows and Linux, and a restore with the
  installed package is tested and confirmed working. Ships in 1.6.1.

- 2026-10-18: Added a `.deb` built by CPack through `scripts/build-deb.sh`, with
  the helper at `/usr/libexec/usb-restoration-helper` and its polkit action. CI
  builds, checks and installs it; releases attach it. Installing it and restoring
  through pkexec is tested and confirmed working. No other package formats are
  planned; the `.deb` and AppImage cover Linux.

- 2026-10-18: The Linux helper catches `SIGPIPE`/`SIGHUP`, and both backends
  confirm the disk again after the partition table is written and before
  formatting. SECURITY.md updated to match. Tested and confirmed working on both
  platforms.

- 2026-10-18: Clarified cancellation feedback about remounting and drive letters.
  Windows Release build and core tests passed.
- 2026-10-18: Consolidated documentation into `docs/`, removed the Obsidian vault,
  hardware-test log and signing guide, and shortened the README. Retired hardware
  coverage tasks following confirmation of testing across devices and systems.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Design boundaries and
intentional exclusions are in [polkit-helper.md](polkit-helper.md) and
[SECURITY.md](../SECURITY.md).

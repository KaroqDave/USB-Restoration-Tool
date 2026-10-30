# Tasks

## Next

### Release preparation

1. Perform the final pre-release review across Fedora RPM, Debian, AppImage and
   Windows builds. Require passing tests and packaging/startup checks, verify
   versions and checksums, and record any remaining validation limits before
   approving a release. Use only disposable media for manual restore checks.

### Other follow-ups

- Measure restore times on disposable media using the setup and step durations
  now recorded in the application log.

## Recently completed

- 2026-10-30: Finished the release cleanup. Removed the unused
  `restoredLocationNoun()` from `DiskService` and both backends. The README
  screenshots now come from a new `rendersTheMainWindow` GUI test that writes
  `app-light.png` and `app-dark.png` with first-run settings, so they show 1.8.0,
  GPT default and the current interface. Updated `AGENTS.md` for the restore
  dialog and the AppImage probe, the helper protocol example, and the screenshot
  instructions. Linux build and all four ctest suites pass at 100% and 200%
  scaling. No physical drive accessed.

- 2026-10-30: Fixed the local AppImage startup crash by rebuilding with upstream
  patchelf commit `3e996db`; linuxdeploy's bundled version moved Qt plugin code
  without preserving its startup address. Packaging now checks bundled platform
  and SVG loading with a backend-free probe, which reproduces the old crash and
  passes on the rebuilt bundle. The actual AppImage passes a four-second
  unprivileged, headless startup check and checksum verification. No restore performed.

- 2026-10-30: Added CPack Fedora RPM packaging with system Qt, formatter
  dependencies, the helper, polkit policy and license. Shared CI/release
  integration builds on Fedora 43 and publishes the RPM and checksum. Fedora 43
  container and Fedora 44 local builds pass all four ctest suites and all 65
  GUI checks at 200% scaling. Fedora 43 install, release-0.1-to-1 upgrade and
  removal pass, including package integrity, unprivileged helper/Qt/SVG probes
  and polkit action loading. Package paths, permissions, dependencies, versions,
  prefix refusals and checksums pass. Updated native-package guidance. GitHub
  workflow execution, desktop authentication and physical-media restore checks
  remain pending; privileged loop tests skipped. No physical drive accessed.

- 2026-10-30: Prepared 1.8.0 in `CMakeLists.txt` with unreleased README notes and
  changelog entries for the restore dialog, setup improvements, clearer errors
  and the About dialog. Package metadata and artifact naming derive from the
  project version. Linux build and all four ctest suites pass; production
  main-window and About widgets show 1.8.0 in both themes using an example
  service. The helper announces app 1.8.0 with protocol 4 unchanged. Release
  preflight accepts v1.8.0, extracts its notes and rejects a mismatched tag.
  Windows builds, package rebuilds and final release validation remain pending;
  privileged loop tests skip locally. No physical drive accessed.

- 2026-10-30: Reviewed the restore dialog against both backends. Confirmation still
  freezes the request shown, and dismissal cannot start a restore. Progress,
  cancellation, failure and success stay in the dialog; the main window rescans
  afterward. Cancel is refused from the first disk-writing step on the Linux and
  Windows plans, matching the backends' last safe checkpoints. Closing is still
  refused while the restore thread runs. The close explanation now says the disk
  is unchanged before that step, and is parented to the restore dialog so it is
  not hidden behind it. Linux build and all four ctest suites pass. The GUI suite
  reports 77 passed at 100% and 200% scaling, in both themes, including both
  plans. Confirmation, progress, success and failure previews were checked in
  both themes, with confirmation and Windows progress also checked at 200%.
  Windows and sanitizer CI have not run for this dialog; the ASan runtime is not
  installed locally. Privileged loop tests were skipped. No physical drive accessed.

- 2026-10-30: Refreshed the About dialog for the single restore window, the
  filesystem choices and the Windows, Debian, Fedora and AppImage builds. The
  displayed version still comes from the project version. GUI tests receive that
  version and fail if it falls back to `0.0.0-dev`. Linux build and all four
  ctest suites pass. The new check passes in both themes at 100% and 200%
  scaling, and the GUI suite reports 67 passed at both scales. Windows and
  sanitizer checks remain pending. No physical drive accessed.

- 2026-10-30: Updated [PR #11](https://github.com/KaroqDave/USB-Restoration-Tool/pull/11)
  to describe the restore dialog, faster setup, clearer drive errors, the
  AppImage startup probe, the Fedora RPM and version 1.8.0. The description
  keeps CI run 78 on `9c9037d` separate from later local results. Windows and
  sanitizer jobs have not rerun for the dialog, the Fedora workflow has not run
  on GitHub, and no physical drive was restored.

- 2026-10-30: Removed the main window's old progress bar and duplicate restore
  outcomes. Progress, cancellation, stall notices and results stay in the restore
  dialog; the main window rescans and shows current disk eligibility for the next
  restore. Added regressions for all outcomes, a fresh acknowledgement on retry,
  and missing, unsafe or unscannable disks after a restore. Linux build and all
  four ctest suites pass; 65 GUI checks pass at 100% and 200% scaling, covering
  both themes. Windows and sanitizer checks remain pending; privileged loop tests
  are skipped locally. No physical drive accessed.

- 2026-10-29: Added a themed, single-window confirmation and restore flow,
  with animated active steps, completed-step checks and durations, elapsed time,
  stall notices and inline results. Confirmation retains the disk details,
  large-drive warning and checkbox gate; dismissal cannot start a restore, and
  the worker receives the exact request shown. It displays the backend's plan and disables
  cancellation once writing starts; Escape and closing cannot dismiss a running
  restore. Linux build and all four ctest suites pass, including 50 GUI checks
  using an example service. Checked both themes and plans at 100% and 200%
  scaling. Windows and sanitizer validation remain pending. No physical disk accessed.

- 2026-10-29: Fixed the Ubuntu 22.04 test build by including glibc's mount header
  before the kernel filesystem header, whose macros otherwise replace the mount
  enum names. The initial PR #11 CI run confirmed the Windows build, tests and
  portable bundle, plus all 37 privileged Linux cases with no skips in both the
  Debian and sanitizer jobs, including the new mounted-partition checks.

- 2026-10-29: Preflight and Linux handle checks skip filesystem display details
  while still reading fresh identity, state and mount points. Windows storage
  operations reuse one WMI connection per restore; permanent physical-drive lock
  errors no longer spend five seconds retrying. Linux signature clearing uses
  4 MiB chunks, reducing the usual eight synchronous writes to two without
  changing the cleared range or flushes. The log records setup, step and total
  durations. Added sector-alignment/range tests, a loop-image mount/identity
  regression and a Windows lock-delay regression. Linux build, 156 core tests,
  eight Linux tests and fuzz-corpus replay pass; 29 loop cases skip locally.
  Windows and privileged loop validation passed in CI as noted above. No physical drive tested.

- 2026-10-29: Windows raw-device errors now name the disk and operation and give
  advice for busy, write-protected, disconnected, not-ready, I/O and capacity
  failures, retaining the Windows error number. Storage and formatter failures
  keep Windows' details alongside the disk, operation and retry advice.
  Added Windows tests for the wording, preserved details and invalid raw handles.
  Linux build and all three ctest suites pass; privileged loop tests are skipped.
  Windows validation passed in CI as noted above. No physical-drive restore was performed.

- 2026-10-28: Linux restore errors now name the drive, the operation and the next
  step instead of quoting `strerror`. `deviceErrorText()` in `src/linux/block_device.*`
  maps `EBUSY`, `EACCES`/`EPERM`, `EIO`, `ENXIO`/`ENODEV` and `ENOSPC`, and is used by
  `BlockDevice` and `linux_restore.cpp`. `LoopDevice` uses `loopErrorText()` in
  `src/linux/loop_device.*` so loop-control and mapping failures are described as loop
  driver problems; `EIO` is described as a drive write failure because the I/O error
  usually comes from the drive underneath the mapping.
  `explainsDeviceFailuresInPlainLanguage` and `explainsLoopFailuresInPlainLanguage` pin
  the wording. Every device message gives the errno, then the next step, as a full
  sentence. `EPERM`, `EACCES` and `EROFS` point to a write-protect switch or a drive
  that made itself read-only, since the kernel refuses writes to a read-only disk with
  `EPERM`; `ENOSPC` warns that the drive may be smaller than it claims. Formatter
  failures keep mkfs's output and advise a retry without calling the drive damaged,
  since mkfs also fails for reasons of its own; `reportsFormatterFailuresWithoutDiagnosingDriveDamage`
  runs a failing stand-in formatter with and without output. Linux build, core tests
  (156) and the non-root Linux tests pass locally. PR #10 CI passes on all four jobs;
  the Debian and sanitizer jobs each ran 33 Linux tests with none skipped.
  No physical-drive restore was performed, and a drive turning read-only part way
  through cannot be reproduced on a loop device.

- 2026-10-24: Published 1.7.2 as the latest stable release after all five release
  jobs passed. Verified the downloaded Windows ZIP, AppImage, Debian package and
  both checksum files against GitHub's asset digests, both Linux checksums, all
  20 published Windows bundle files and the executable's 1.7.2 version. The
  merged PR branch has been removed locally and on GitHub, and the local
  release branch after fast-forwarding main.

- 2026-10-24: Prepared 1.7.2 with release notes for the Linux restore fixes,
  loop-device retry and Windows storage-job logging. Helper protocol stays at 4.
  Windows Release build, all three test suites and the release preflight pass.
  Verified the local ZIP's executable version and all 22 bundled file checksums;
  only the executable, README.txt and SHA256SUMS.txt differ from 1.7.1's bundle.
  Platform release checks run in CI.

- 2026-10-24: Settled the review finding on `waitForStorageJob`: a vanished job or
  a completed job without `ErrorCode` still stops the restore. The restore log now
  says whether each `MSFT_Disk.Refresh`, `MSFT_Partition.DeleteObject` and
  `MSFT_Volume.Format` call completed immediately or was queued, and how a queued
  job ended. Four restores on an Intenso Office Line stick (Windows 11 build
  26300; GPT exFAT, FAT32, NTFS and exFAT again) all completed, with every one of
  the 16 calls completing immediately, so no job was queued. Keeping the strict
  check means an unconfirmed job costs at worst a re-run, never a false success.
  `describesStorageJobOutcomes` pins the log text. Windows Release build, WMI (24)
  and core (155) tests pass.

- 2026-10-24: Closed the remaining v1.7.1 review cleanups. A busy loop node no
  longer stalls the formatter mapping: after `EBUSY` it asks loop-control for a
  new node, and a failed `configure` leaves no path behind.
  `BlockDevice::verifyIdentity` drops its redundant sequence check. The preflight,
  table writer and Windows `RawDisk` share `partitionLayoutError`. The sequence
  refusal names the disk, the `isSameRestoreTarget` comments describe the
  sequence rule, and AGENTS.md lists all three test files. Locally the Windows
  Release build and core tests (155) pass. PR #9 CI passes on commit `5477660`:
  Windows core, WMI and fuzz-corpus tests, and `skipsLoopNodeClaimedElsewhere`
  in both the Debian and sanitizer Linux jobs. No physical-drive restore was
  performed.

- 2026-10-24: Fixed three Linux restore regressions from 1.7.1. A formatter
  failure now still re-reads the partition table, the new partition is checked
  against the disk sequence read after the re-read, and the read-only identity
  handle is closed before unmounting. `formatAndRereadPartitionTable` holds the
  Format and re-read steps; `rereadsPartitionTableAfterFormatting` runs them on a
  partition-scanning loop image at both sector sizes, with mkfs failing and
  succeeding. PR #9 CI passes on commit `5477660`: all four cases pass in the
  Debian and sanitizer jobs, each running 30 Linux tests with none skipped. A
  real media change cannot be triggered on a loop device. No physical-drive
  restore was performed.

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
  through pkexec is tested and confirmed working.

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

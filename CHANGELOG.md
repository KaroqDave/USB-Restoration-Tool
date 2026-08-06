# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 1.0.0 - 2026-08-06

A rebuild of the tool around the same safety model, carried out more carefully.

### Added

- Identity verification through the open `\\.\PhysicalDriveN` handle — device
  number, bus type, byte length, sector size, serial number and writability —
  run before the first destructive step and again immediately before it.
- Cancellation of a restore while it is still reversible, refused once it is
  not.
- Step-by-step progress across the 14 stages of a restore, replacing an
  indeterminate busy bar.
- Light, dark and system themes matching ISO Integrity Check, with the theme
  and window geometry remembered between runs.
- A summary dialog before the restore listing the drive letters and volume
  labels about to be erased.
- Refusals for write-protected disks, disks under 8 MB, disks reporting an
  unsupported sector size, disks holding the Windows drive or the drive the app
  itself runs from, and disks too small for a valid GPT layout.
- An in-app **Open log file** action, and log rollover at 1 MB.
- A `release.yml` workflow that gates on the tag matching `PROJECT_VERSION` and
  builds the release body from the README.
- Tests for the tightened safety rules, the identity comparison, and 4096-byte
  sector layouts.

### Fixed

- Every `uint16` property read from WMI came back as its fallback value.
  `BusType`, `PartitionStyle` and `HealthStatus` were affected; the disk list
  showed a blank partition style and health, and the USB-bus safety check was
  comparing against a bus type that was always `0` — which it then allowed
  through as "unknown is acceptable".
- `MSFT_Storage` methods returning `4096` ("queued as a job") were treated as
  completed. Disk refresh, partition deletion and formatting now wait for the
  job to finish before the next step runs.
- The restore worker entered a single-threaded COM apartment and then made
  cross-apartment WMI calls without a message loop to pump them, which could
  hang a restore. It now uses a multi-threaded apartment.
- The target-identity check stopped at the first strong identifier that
  matched, so a device that kept its serial number while changing its device
  path was accepted as unchanged. Every identifier both sides report must now
  agree.
- Raw writes are aligned to the reported sector size, and the head and tail
  clearing windows can no longer overlap on a small disk.
- Progress messages from the worker were connected to an empty slot and
  discarded.
- Drive letters were opened with read/write access purely to ask which disk
  they belong to; ownership is now decided on a query-only handle and write
  access is requested only for a letter that has already proved to be on the
  target.
- The volume label is applied by the format itself instead of afterwards,
  closing a window where the restored drive was mounted under the old
  filesystem's name.

### Changed

- The executable is now `usb-restoration-tool.exe`, and the repository layout,
  formatting, and CI match the other repositories in this account.
- `src/core` holds the safety policy and layout arithmetic with no Windows
  headers behind it, so those rules are testable on their own.
- The log moved from `Documents` to
  `%LOCALAPPDATA%\KaroqDave\USB Restoration Tool\usb-restoration-tool.log`.
- COM is initialised process-wide with packet-privacy authentication, and the
  DLL search order excludes the current directory and `%PATH%`.
- Release builds are compiled with `/sdl /guard:cf` and linked with
  `/guard:cf /CETCOMPAT`.
- `scripts/build-release.ps1` and `scripts/package-release.ps1` are replaced by
  a single `scripts/build-standalone.ps1`.

### Removed

- The unreachable self-elevation path. The manifest requires elevation, so a
  process that reaches `main()` without it is in a state to stop in, not to
  relaunch from.
- Committed VS Code tasks and settings, and the hand-drawn SVG mockups that
  were presented as screenshots.

## 0.1.0 - 2026-05-25

- Initial Windows-native Qt 6 release.
- Restores selected USB disks to one GPT + exFAT volume labelled `USB`.
- Uses direct Windows disk APIs for raw disk cleanup and GPT layout creation.
- Uses WMI Storage APIs for disk metadata refresh, partition deletion, and
  exFAT formatting.
- Safety checks for boot/system disks, `C:`, stale target identity,
  read-only/offline media, and large USB disks.
- Portable Windows x64 ZIP with Qt/MSVC runtime dependencies and full-bundle
  SHA256 hashes.

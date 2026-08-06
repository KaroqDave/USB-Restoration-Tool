# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Unreleased

### Changed

- **The Linux GUI no longer runs as root.** An installed build runs the
  application as an ordinary user and performs the restore in a separate
  `usb-restoration-helper`, started through `pkexec` and holding privilege for
  the duration of one restore. It links Qt Core and nothing else, so none of the
  widget, theming or image-format machinery is loaded as root. The AppImage is
  unchanged and still runs everything as root: polkit reads its policies from
  system directories, which a self-contained bundle cannot register one in.
- **The helper trusts nothing it is told.** It re-enumerates the disk from
  sysfs, rebuilds its own `RestoreGuard`, and re-runs every safety rule before
  anything is opened. The arguments it receives are claims it can only refuse
  on — notably, the caller cannot assert that its target is on the USB bus.
- **`detectPartitionStyle()` falls back to udev's database** when the device
  node cannot be opened, so the unprivileged GUI still shows GPT or MBR rather
  than "RAW / unknown" for every disk.

> None of this has been exercised against a physical USB stick, and the pkexec
> round trip has not been exercised at all. See `docs/polkit-helper.md`.

## 1.1.0 - 2026-08-06

Linux support, a choice of partition style, and a shorter window.

### Added

- **A Linux backend**, shipped as a portable AppImage. USB disks are enumerated
  from sysfs, mounts read from `/proc/self/mountinfo`, the disk opened with
  `O_EXCL` — which the kernel refuses while anything is still mounted — the
  partition table written directly, and the new partition handed to
  `mkfs.exfat`. Needs root.
- **A `DiskService` interface** in `src/platform`. Everything platform-specific
  sits behind it, so the GUI and the safety rules compile unchanged on either
  platform and a third would only need one more implementation.
- **GPT or MBR**, selectable in the Restore card and remembered between runs.
  MBR is for devices that refuse to read a GPT stick at all.
- **`src/core/partition_table.cpp`**: GPT and MBR serialisation as pure
  functions over a buffer, including the CRC-32 that GPT specifies. Used by the
  Linux backend, which has no equivalent of Windows'
  `IOCTL_DISK_SET_DRIVE_LAYOUT_EX`.
- **`scripts/verify-partition-tables.sh`** and the `usb-partition-dump` tool:
  the tables a restore would write are dumped into sparse images and handed to
  `sgdisk` and `fdisk`, so they are checked against the tools that will have to
  read a restored disk rather than only against the specification. CI runs it
  on every push.
- Per-platform startup hardening: `PR_SET_DUMPABLE` off and a restrictive umask
  on Linux, alongside the existing DLL search-order narrowing on Windows.

### Changed

- **The activity log moved into its own window**, behind an **Activity** button
  in the header. The main window is about a third shorter and fits on a laptop
  screen without scrolling.
- **The typed confirmation phrase is gone.** The summary dialog that already
  named the disk now carries an acknowledgement checkbox instead, so the gate
  sits on the screen that describes the consequence.
- **The log lives beside the executable**, falling back to per-user application
  data only when that directory is not writable.
- `DiskInfo` is keyed on a platform device path rather than a Windows disk
  number, and drive letters generalised to mount points.
- The partition length is rounded down to a 1 MiB boundary so both ends align,
  which is what every other partitioning tool produces and what `sgdisk -v`
  expects. It costs under a megabyte.
- The MBR carries a real disk signature rather than zeros; Windows tells disks
  apart by it.
- Linux builds are compiled with `-D_FORTIFY_SOURCE=2 -fstack-protector-strong`
  and linked with `-pie -Wl,-z,relro,-z,now,-z,noexecstack`.

### Fixed

- The desktop automounter could win two races on Linux, both found by comparing
  against Fedora Media Writer — which is immune to them only because UDisks2
  both unmounts the device and holds it. Unmounting a stick is exactly the event
  that prompts GNOME or KDE to mount it straight back, so the exclusive open now
  retries with a fresh unmount; and a newly published partition is exactly what
  an automounter grabs, so it is unmounted again before `mkfs.exfat`, which
  refuses a mounted device.
- `readMounts()` returned nothing at all. procfs reports every file as zero
  bytes and `QTextStream::atEnd()` believes it, so the read loop exited before
  the first line. Every mount point was therefore invisible and the guard that
  refuses the disk the running system boots from was empty.
- An empty entry in the protected-device list matched every disk rather than
  none, from a negated helper that read as its own opposite.
- A protected drive letter written as a bare `C` did not match a disk reporting
  `C:`, which is exactly the form the Windows guard supplies.

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

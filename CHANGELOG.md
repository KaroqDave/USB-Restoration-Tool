# Changelog

All notable changes to this project are documented here. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/), and the project uses
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## 1.8.0 - Unreleased

A single confirmation-to-result restore dialog, reduced restore setup work and
clearer drive errors.

### Added

- A themed restore dialog with animated active steps, completed-step durations,
  elapsed time, stall guidance and inline success, cancellation and error results.
- Application log timings for restore setup, each numbered step and the entire
  attempt, including failed and cancelled attempts.
- GUI regressions for restore outcomes, fresh acknowledgement on retry and
  missing, unsafe or unscannable drives after a restore.
- A native Fedora RPM built with CPack, including the restore helper, polkit
  action, system Qt and formatter dependencies. CI and release builds check
  installation, upgrade and removal in a disposable Fedora environment.
- A backend-free AppImage packaging probe for bundled Qt platform and SVG
  loading, detecting plugin corruption before a bundle is published.

### Changed

- Erase confirmation and progress share one dialog. The disk summary,
  large-drive warning and acknowledgement gate remain in place; cancellation
  stops being available when destructive work starts, and a running restore
  cannot be dismissed.
- The main window rescans after a restore and shows current drive eligibility
  instead of duplicating the restore dialog's progress and result.
- Preflight and Linux open-handle checks omit filesystem display details while
  still checking fresh identity, disk state and mount points.
- Windows storage operations reuse one WMI connection per restore.
- Linux signature clearing uses 4 MiB writes without changing the cleared range
  or flushes.
- Linux and Windows drive failures name the operation and provide next steps,
  while retaining OS error details. Formatter failures preserve their output
  without assuming that the drive is damaged.

### Fixed

- Windows no longer retries permanent physical-drive lock errors for five
  seconds.
- The local AppImage startup crash caused by patchelf rewriting Qt plugins was
  resolved with a corrected patchelf build. Packaging supports selecting that
  build through `PATCHELF` and rejects bundles that fail the loading probe.

## 1.7.2 - 2026-10-24

Fixes for Linux restore regressions introduced in 1.7.1, and a more detailed
Windows restore log.

### Fixed

- Linux re-reads the partition table even when the formatter fails, so the
  kernel no longer keeps the old partitions over the new table.
- Linux checks the new partition against the disk sequence read after the
  partition-table re-read, so a media-change event no longer fails a finished
  restore.
- Linux closes the read-only identity-check handle before unmounting instead of
  holding it for the whole restore.
- Linux asks for a new loop device when the free one is held exclusively by
  another program, instead of retrying the same busy device until it fails.
- The disk-instance refusal names the disk.

### Changed

- The restore preflight, the partition-table writer and Windows partition
  creation share one set of layout checks.
- The Windows restore log records whether each `MSFT_Disk.Refresh`,
  `MSFT_Partition.DeleteObject` and `MSFT_Volume.Format` call completed
  immediately or was queued, and how a queued job ended. A queued job still
  needs an explicit completed state and zero error code.

### Added

- Linux image tests for partition re-reads after formatter failure and success
  at both sector sizes, and for a loop device held by another program.
- Tests that the preflight and the table writer agree on every layout, and for
  the Windows storage-job log text.

## 1.7.1 - 2026-10-23

Stricter restore preflight, device identity and storage-job checks on Windows
and Linux.

### Fixed

- MBR layouts that exceed the disk's sector-addressing limit are refused before
  destructive work, including Windows partition deletion and signature clearing.
- Linux compares the selected disk instance with the open device and freshly
  enumerated identity before writing. Formatting stays bound to the verified
  device through a temporary mapping, so a reused device pathname cannot redirect it.
- Missing or unreadable Windows storage jobs no longer count as successful
  completion. A queued operation needs an explicit completed state and zero error.
- The restore refusal message covers unsupported partition layouts as well as
  filesystems. Windows ZIP instructions now describe the current erase checkbox
  and partition/filesystem choices.

### Changed

- Linux restores require kernel 5.15 or later with disk-sequence support and the
  loop driver. The app and helper use protocol 4 and must be upgraded together.

### Added

- Regression tests for layout refusal before backend work, changed or unavailable
  disk instances, helper arguments and WMI job monitoring.
- Linux image tests for all four filesystems at 512-byte and 4096-byte sectors
  with file and exclusive block-device backing. Cleanup tests cover cancellation
  before erasure, formatter failure and launch failure. CI also runs the image
  suite under ASan/UBSan.

## 1.7.0 - 2026-10-22

Refreshed branding and a clearer restore interface, with Windows FAT32
formatting repaired.

### Fixed

- The erase acknowledgement checkbox has a larger, clearly outlined indicator,
  a contrasting checkmark and a visible keyboard focus state in both themes.
- Windows FAT32 restores no longer fail while setting `AllocationUnitSize` with
  WMI error `0x80041005`. Numeric method parameters now use WMI's required COM
  types, covered by in-memory regression tests.

### Changed

- New USB and restore-arrow icon across Windows, Linux and the app, with matching
  project artwork and a mint and ink palette in light and dark themes.
- Drive selection and restore settings sit side by side, with drive details,
  a persistent erase warning and a branded About dialog.

## 1.6.3 - 2026-10-22

Windows restores USB sticks without a partition table and dismounts volumes
that have no drive letter.

### Added

- The Activity log reports how often Windows refused the first write as
  "device not ready" while removing the old volume, if any retries were needed.

### Fixed

- On Windows, a USB stick with no partition table (a "superfloppy", such as a
  stick that only holds a bootloader written straight to the disk) can be
  restored. It used to stop with `MSFT_Partition.DeleteObject failed with return
  value 1`, then with "device not ready" (error 21) on the first write: Windows
  invents a whole-disk partition for such a stick and holds an offline volume
  on it that refuses raw writes. Every restore now writes an empty GPT through
  the verified disk handle once the partitions are deleted, and waits for
  Windows to remove the old volumes before clearing the disk. This also covers
  a stick with GRUB boot code that only turns into a superfloppy once its
  partitions are gone.
- On Windows, volumes on the target disk that are mounted at a folder or have
  no drive letter are now dismounted before the disk is changed, as volumes
  with a drive letter already were.

## 1.6.2 - 2026-10-21

Stricter checks on what passes between the Linux app and its restore helper,
with the parsers on that boundary now fuzzed in CI.

### Added

- `usb-core-fuzz`, a fuzz harness for the helper's argument parser, the
  protocol line parser and the partition-table builder. ctest replays its seed
  corpus on every platform, and a new CI job runs the core tests and partition
  table checks under ASan and UBSan, then fuzzes for two minutes.

### Fixed

- The restore helper refuses `--expect-size` and `--expect-sector-size` values
  with a sign or surrounding whitespace, such as `+16` or `512 `, instead of
  reading them as numbers. Only the plain digits the GUI writes are accepted.
- A version announcement that names a detail twice, such as two step counts, is
  refused rather than taking the last one.
- The Linux GUI no longer lets a second version announcement from the helper
  undo the refusal of the first. Any second announcement now fails the restore
  and asks the helper to stop.
- A step, detail or result line with an empty message is read back as that
  line instead of being shown as unrecognised helper output.
- Protocol text is cut at its 2000-character limit without going one character
  over, which a space before the last word could cause, and without ending in
  half of a surrogate pair, which did not survive the UTF-8 trip to the GUI.

## 1.6.1 - 2026-10-18

A Linux GUI and restore helper from different builds are now refused before
anything is asked or written.

### Fixed

- A Linux GUI and restore helper from different releases are now refused
  before the password prompt, with an error naming both versions. The helper
  announces its step count and first destructive step along with the protocol
  version (now 3), because the GUI uses its own copy of those numbers to say
  whether unplugging the stick is still safe.

### Changed

- Each backend's restore steps are listed once in `src/core/restore_steps.cpp`.
  The step count and first destructive step are derived from that list and
  pinned by tests, and a backend that reports a step out of order is logged.

## 1.6.0 - 2026-10-18

A Debian package that keeps the Linux GUI unprivileged, and a second identity
check before formatting.

### Added

- A `.deb` package for Ubuntu 24.04 and Debian 13 or later, built with CPack by
  `scripts/build-deb.sh`. It installs the GUI, `usb-restoration-helper` in
  `/usr/libexec` and its polkit action, so the restore runs through pkexec
  instead of the whole app running as root. It depends on the system Qt,
  pkexec and the four formatters. CI builds it, checks its contents and
  installs it; release builds attach it.

### Fixed

- The Linux helper no longer dies when the GUI goes away mid-restore. A
  progress line written to a closed pipe raised `SIGPIPE`, whose default action
  could stop the helper between the partition table and `mkfs` and leave a
  blank stick; `SIGPIPE` and `SIGHUP` are now caught.
- The disk is confirmed again after the partition table is written, before the
  new volume is formatted. Linux re-enumerates the disk, re-verifies its node,
  ties the partition node to it through sysfs and, on kernel 5.15 and later,
  compares the disk sequence number. Windows asks the open handle again before
  assigning a drive letter and before formatting. Previously both found the new
  volume by name or disk number alone, after a wait long enough for a different
  stick to take either.
- Cancellation feedback now explains that existing volumes may need to be
  mounted again or have their drive letters reassigned, instead of claiming
  that nothing changed.

### Documentation

- SECURITY.md describes what happens after the partition table is written on
  each platform, covers the helper's command-line exception, and asks Linux
  reporters for their distribution and how the tool was run.
- Consolidated contributor guidance and open tasks under `docs/`.
- Removed the Obsidian vault, hardware-test logs and standalone signing guide.
- Shortened the README and retained detailed release history here.

## 1.5.0 - 2026-08-20

Filesystem selection, clearer progress feedback and the first Linux hardware
validation. The untagged 1.4.x development changes shipped in this release.

### Added

- **The Selected disk panel names the filesystems on the disk.** Linux reads
  udev's records per partition and for the whole-disk device, so an ISO-written
  stick shows its bare `iso9660` too, and the FAT family is named `FAT32` or
  `FAT16` rather than the kernel's `vfat`; Windows takes the name from the same
  call that already fetched the labels. Display only — no safety rule reads it.
- **A stalled device explains itself.** After 15 seconds without a word from
  the backend the progress bar counts the silence; after 60 the status says
  plainly that the device has stopped responding, that sticks often stall for
  minutes and recover, and — strictly before the first write, decided by the
  new `DiskService::firstDestructiveStep()` — that the restore has not yet
  changed the disk, though the system may still be flushing its own writes to
  it. Prompted by a real stick that sat in uninterruptible sleep for four and
  a half minutes while the GUI showed only "Stopping...".
- **The privilege badge states the real mode.** An installed Linux build shows
  USER, because the GUI runs unprivileged and only the helper is root — which
  is the point of the privilege split, and the opposite of what the old
  hardcoded ROOT badge claimed. ROOT and ADMINISTRATOR now appear only when
  the process itself is elevated.

- **Filesystem selector** in the Restore card: exFAT (default), FAT32, NTFS, and
  ext4 on Linux. The list comes from `DiskService`, so the GUI does not know
  which OS it is on.
- Partition type follows the filesystem: MBR `0x0C` for FAT32, GPT Linux
  filesystem GUID for ext4. Windows Format confirms the requested filesystem
  rather than assuming exFAT.
- Helper protocol version 2, with required `--filesystem`. An unknown token is
  refused.
- New ext4 volumes belong to the user who asked for the restore, through
  `mkfs.ext4 -E root_owner=`, taken from `PKEXEC_UID` or `SUDO_UID`. ext4 is the
  only filesystem here that stores ownership on the volume itself; for exFAT,
  FAT32 and NTFS the mount options decide.

### Changed

- On a GNOME Wayland session the app prefers XWayland (`xcb`, with `wayland`
  as fallback), because GNOME refuses server-side decorations and the fallback
  Qt draws itself is a bare strip with no close, minimize or maximize buttons.
  Compositors that provide decorations, and an explicitly chosen
  `QT_QPA_PLATFORM`, are left alone.
- The FAT32 allocation unit follows `format.com`'s own size table — a 1 GiB
  volume gets 4 KiB clusters like Windows would give it, not the smallest
  legal 512 bytes and the 16 MB of FAT that come with it — clamped into the
  legal cluster range, with the smallest legal unit as the fallback.
- The completion message reports the restore that actually ran rather than
  re-reading the filesystem combo box after it had been re-enabled.
- Linux resolves the `mkfs` tool before the first write, so a missing
  `mkfs.vfat` does not leave a blank partition table.
- Windows blocks FAT32 on volumes larger than 32 GiB, which `MSFT_Volume.Format`
  cannot create.
- Dropped the second `mkntfs` attempt on Linux. `-f`/`--fast` is the same flag
  as `-Q`/`--quick`, so the retry could never rescue a run the first form
  failed; it read like the genuine second-dialect coverage the
  exfatprogs/exfat-utils pair provides.
- Linux blocks FAT32 on volumes outside the range `mkfs.vfat` can actually
  describe — under 33 MiB or over 2 TiB with 512-byte sectors, and the
  equivalents for 4096. dosfstools refuses neither end on its own: it warns and
  writes an out-of-spec filesystem below the cluster minimum, and silently
  clamps a disk too large for its 32-bit sector count. The refusal happens
  before anything is erased, and the helper applies it again to the disk it
  re-derives.

### Fixed

- **A cancel during pkexec authentication can no longer be reported as
  "cancelled — the disk was not changed" while the helper completes the
  restore.** Once pkexec authenticates, the helper is root and the
  unprivileged GUI's kill fails silently; the old code believed its own
  signal. The kill is now recorded as a request, a helper that speaks anyway
  is asked to stop through the cancel token, and the run's ending is judged
  by `judgeHelperRun()` in core: a requested kill is only believed as a clean
  cancellation when the process died without ever speaking, and a completed
  restore is reported as completed. Confirmed on hardware.
- **A helper whose protocol version the GUI does not recognise is stopped at
  its first checkpoint**, before anything is written, instead of the mismatch
  being noticed after the run. A rejected version also outranks whatever the
  exit code claims.
- The Windows cancel checkpoint that sat *after* the partition-record
  deletion — where a cancel reported an untouched disk whose partition table
  was already gone — moved above it, and the disk's identity is re-verified
  through the open handle immediately before the first by-number WMI call.
- `writeZeros()` on both platforms refuses a range that is not
  sector-aligned instead of depending on every caller aligning first.
- `scripts/verify-partition-tables.sh` runs under `LC_ALL=C`, so its checks
  work outside an English locale.

## 1.3.0 - 2026-08-06

Privilege separation on Linux. There is no 1.2.0: the split was always planned
for 1.3.0, because doing it properly depends on being installed rather than
bundled, which is a packaging project of its own.

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

> At the time of the 1.3.0 release, Linux hardware and polkit validation were
> still pending. Both paths have since been tested.

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

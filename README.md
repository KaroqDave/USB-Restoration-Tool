# USB Restoration Tool

[![Build](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml/badge.svg)](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/KaroqDave/USB-Restoration-Tool?label=release)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![Platform](https://img.shields.io/badge/platform-Windows%20x64-blue)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

A Windows desktop app that gives a USB drive back after an ISO writer has been over it.

Writing a Linux ISO to a USB stick leaves it in a state Windows barely understands: a hybrid partition table, a read-only ISO9660 volume, a few hundred megabytes of a drive that used to hold 64 GB. This app restores the whole drive to **one GPT partition formatted exFAT and labelled `USB`** — the layout it shipped with.

It drives the Windows storage stack directly through `DeviceIoControl` and the `MSFT_Storage` WMI classes. There is no `diskpart` script and no PowerShell in the restore path.

Built with C++ and Qt 6.

> **Restoring erases every partition and file on the selected disk.** Only USB disks are listed, and boot, system, offline, and read-only disks are refused outright — but the drive you pick is still erased completely.

## What's New in 1.0.0

A rebuild of the whole tool. The safety model is the same idea, carried out considerably more carefully.

- **The disk is now identified through the open handle, not just by number.** Windows reuses disk numbers as soon as a device is unplugged, so everything before this point — the bus check, the size check, the confirmation phrase — described a disk that `\\.\PhysicalDriveN` might no longer name. The restore now re-reads the device number, bus type, byte length, sector size, serial number and writability from the handle it is about to write through, before the first sector is touched and again immediately before it is.
- **Fixed: every 16-bit property Windows reports was being read as zero.** `BusType`, `PartitionStyle` and `HealthStatus` arrive from WMI as `uint16`, which the value conversion did not handle. The disk list showed a blank partition style and an empty health, and — more to the point — the "is this really USB?" safety check was comparing against a bus type that was always 0, which it then allowed through as "unknown is fine". Unknown is no longer fine.
- **Fixed: steps no longer race the step before them.** The `MSFT_Storage` methods return `4096` to mean "queued as a job", which the tool was treating as "done". Refresh, partition deletion and formatting now wait for the job to actually finish.
- **Fixed: WMI could hang the restore.** The worker thread entered a single-threaded COM apartment and then made cross-apartment calls without pumping messages. It now uses a multi-threaded apartment, which is what a worker thread that does not run a message loop needs.
- **A new interface**, matching [ISO Integrity Check](https://github.com/KaroqDave/ISO-Integrity-check): light, dark and system themes, cards, a status badge that says in one line whether the selected disk can be restored and why not when it cannot, and real step-by-step progress instead of a bar that only knew "busy".
- **A restore can be cancelled** while it is still reversible, and refuses to be once it is not.
- **Hardened elsewhere**: packet-privacy COM authentication, a DLL search order that excludes the current directory, control-flow guard and CET, drive letters checked on query-only handles before any write handle is opened, and the volume label applied by the format itself rather than afterwards.

See [SECURITY.md](SECURITY.md) for the full safety model and [CHANGELOG.md](CHANGELOG.md) for the rest.

## Features

- **USB disks only.** The disk list comes from a WMI query filtered to the USB bus, and the bus is checked again through the raw device handle before anything is written.
- **Layered refusals.** Boot disks, system disks, disks holding the Windows drive or the drive the app is running from, offline disks, read-only disks, write-protected disks, disks too small for a valid GPT layout, and disks reporting an unsupported sector size are all refused rather than warned about.
- **A confirmation phrase that names the disk**: `RESTORE DISK 2` arms disk 2 and nothing else, followed by a summary dialog listing the drive letters and volume labels that are about to disappear.
- **An extra warning above 128 GB**, because a "USB disk" that size is more often an external SSD full of backups than a boot stick.
- **Revalidation at every step**, including a final identity check on the open handle immediately before the first destructive write.
- **Step-by-step progress** across the 14 stages of a restore, with a log written to disk for when one of them fails.
- **Light, dark and system themes**, remembered between runs along with the window geometry.

## Download

Grab the latest build from the [Releases page](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest):

1. Download `USB-Restoration-Tool-<version>.zip`.
2. Verify it against `SHA256SUMS.txt` in the bundle if you want to check it arrived intact.
3. Extract it anywhere and run `usb-restoration-tool.exe`.
4. Approve the Windows Administrator prompt. Raw disk access is not possible without it.

Keep the DLLs and plugin folders next to the executable. If Windows reports missing runtime DLLs, install the latest [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Releases can be distributed unsigned or Authenticode-signed. Unsigned builds may trigger SmartScreen warnings; see [docs/signing.md](docs/signing.md).

## How To Use

1. Plug in the USB drive and start the app. Detected USB disks are listed with their size, current partition style, and drive letters.
2. Select the disk. The **Selected disk** card shows what Windows reports about it, and the status badge says whether it can be restored — or why it cannot.
3. Type the confirmation phrase shown under the disk, for example `RESTORE DISK 2`.
4. Choose **Restore USB** and confirm the summary dialog.

The restore takes a few seconds on a typical stick. **Cancel** stops it while it is still reversible; once the first sector has been rewritten it runs to completion, because a half-written partition table is worse than a finished one.

Afterwards the drive is one exFAT volume labelled `USB`, with a drive letter assigned.

### When something goes wrong

Every step is written to a log file, reachable from **Open log file** in the app:

```text
%LOCALAPPDATA%\KaroqDave\USB Restoration Tool\usb-restoration-tool.log
```

The most common failure is a drive letter that cannot be locked because a file browser, antivirus scanner, or indexing service still has it open. Close anything looking at the drive and try again.

## Safety Model

Nothing here is a warning that can be clicked through; each one is a refusal.

| Check | Refused when |
|-------|--------------|
| Bus type | The disk is not reported on the USB bus, by WMI **and** by the raw device handle |
| Boot / system | Windows marks the disk as either |
| Drive letters | The disk holds `C:`, the Windows drive, or the drive the app is running from |
| State | The disk is offline, read-only, or write-protected |
| Geometry | The disk is under 8 MB, reports a sector size other than 512/1024/2048/4096, or is too small for a valid GPT layout |
| Identity | The disk number, size, sector size, serial number, unique ID, device path, or model changed between selection and restore |
| Confirmation | The phrase naming that exact disk number has not been typed |

The confirmation phrase, the summary dialog, and the identity check are three separate gates, and the last of them is repeated on the open handle immediately before the first write.

## Build From Source

Prerequisites:

- Windows x64.
- CMake 3.21 or newer.
- Visual Studio 2022 or newer with the MSVC x64 toolset (Build Tools are enough).
- Qt 6.8 or newer with the `Core` and `Widgets` components, plus `Test` to run the C++ tests.

Point CMake at your Qt kit, either as an environment variable or on the command line:

```powershell
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.11.1\msvc2022_64"
```

If Qt is installed under `C:\Qt` and neither `CMAKE_PREFIX_PATH` nor `Qt6_DIR` is set, the build locates the newest MSVC kit itself.

### Build with presets

```powershell
cmake --preset windows
cmake --build --preset windows
ctest --preset windows
```

`windeployqt` runs post-build, so the Qt runtime DLLs and plugins land beside the executable. Output: `build\Release\usb-restoration-tool.exe`.

### Build without presets

```powershell
cmake -S . -B build -DCMAKE_PREFIX_PATH="C:\Qt\6.11.1\msvc2022_64"
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Configure with `-DUSBRESTORE_BUILD_TESTS=OFF` to build just the app.

### Standalone bundle

```powershell
./scripts/build-standalone.ps1
```

This builds Release, exports the executable with the full Qt runtime to `standalone\USB-Restoration-Tool`, writes `SHA256SUMS.txt` covering every bundled file, and zips the folder to `standalone\USB-Restoration-Tool-<version>.zip`. The `standalone/` folder is regenerated on each run and is not tracked in git.

Add `-Sign -CertificateThumbprint <thumbprint>` to Authenticode-sign the executable, and `-RequireSignature` for a pipeline that must fail on unsigned output. See [docs/signing.md](docs/signing.md).

## Tests

```powershell
ctest --test-dir build -C Release --output-on-failure
```

The tests cover `src/core` — the formatting, the safety refusals, the target-identity comparison, and the GPT layout arithmetic. That is deliberately the half of the codebase with no Windows headers behind it, so the rules that decide whether a disk may be erased can be exercised without a disk.

Everything under `src/win` talks to real hardware and is not covered by automated tests. **Do not test a restore on a drive you are not willing to lose.**

## Project Layout

```text
src/core    Safety policy, disk formatting, GPT layout arithmetic — no Windows headers
src/win     Raw disk I/O, WMI, disk enumeration, the restore worker
src/gui     Qt widgets, theme, settings
tests       Tests for src/core
scripts     Standalone bundle packaging
```

## License

[MIT](LICENSE).

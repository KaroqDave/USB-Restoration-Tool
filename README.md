# USB Restoration Tool

[![Build](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml/badge.svg)](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/KaroqDave/USB-Restoration-Tool?label=release)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-blue)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

A desktop app that gives a USB drive back after an ISO writer has been over it.

Writing a Linux ISO to a USB stick leaves it in a state the system barely understands: a hybrid partition table, a read-only ISO9660 volume, a few hundred megabytes of a drive that used to hold 64 GB. This app restores the whole drive to **one partition formatted exFAT and labelled `USB`** — the layout it shipped with — under either **GPT** or **MBR**.

It drives each platform's storage stack directly: `DeviceIoControl` and the `MSFT_Storage` WMI classes on Windows, sysfs and raw block-device I/O on Linux. There is no `diskpart`, no PowerShell, and no shelling out to `sfdisk`.

Built with C++ and Qt 6.

> **Restoring erases every partition and file on the selected disk.** Only USB disks are listed, and boot, system, offline, and read-only disks are refused outright — but the drive you pick is erased completely.

## What's New in 1.1.0

- **Linux support.** A native backend built on sysfs, `/proc/self/mountinfo`, and raw block-device I/O, shipped as a portable AppImage. It enumerates USB disks, refuses the ones the running system depends on, unmounts what it is about to erase, writes the partition table itself, and hands the new partition to `mkfs.exfat`. Run it with `sudo`.
- **GPT or MBR.** A layout selector in the Restore card, defaulting to GPT. MBR is there for the devices that refuse to read a GPT stick at all: BIOS-era PCs, car stereos, older TVs and set-top boxes. The choice is remembered between runs.
- **The activity log moved into its own window,** behind an **Activity** button in the header. The main window is about a third shorter as a result and fits on a laptop screen without scrolling.
- **The typed confirmation phrase is gone.** In its place, the summary dialog that already named the disk now carries a checkbox to acknowledge what is about to happen. One gate instead of two, on the screen that actually describes the consequence.
- **The log lives beside the executable,** so a portable copy carries its own history and leaves nothing behind on the machines it is plugged into. It falls back to per-user application data only when the application folder is read-only, such as an AppImage's mount or an install under Program Files.
- **The partition tables are verified against the tools that read them.** `scripts/verify-partition-tables.sh` writes what a restore would produce into a sparse image and hands it to `sgdisk` and `fdisk`; CI runs it on every push. See [Testing](#testing).

> **The Linux build has not been tested on real hardware.** It compiles clean, passes the test suite, and every partition table it can write has been validated with `sgdisk` and `fdisk` — but no restore has run against a physical USB stick on Linux. The checks that stop it touching the wrong disk are the well-covered part; the sequence that erases the right one is not. Treat 1.1.0 on Linux as a first cut and use a drive you can afford to lose. The Windows path is unchanged from 1.0.0 and has been verified on hardware.

### Previously, in 1.0.0

- **The disk is identified through the open handle, not just by number.** Windows reuses disk numbers as soon as a device is unplugged, so everything before that point described a disk that `\\.\PhysicalDriveN` might no longer name. The restore re-reads device number, bus type, byte length, sector size, serial number and writability from the handle it is about to write through.
- **Fixed: every 16-bit property Windows reports was being read as zero,** including the bus type the "is this really USB?" check compared against.
- **Fixed: steps raced the step before them.** `MSFT_Storage` methods return `4096` to mean "queued as a job", which was being treated as "done".
- **A rebuilt interface** matching [ISO Integrity Check](https://github.com/KaroqDave/ISO-Integrity-check): light, dark and system themes, and real step-by-step progress.

See [CHANGELOG.md](CHANGELOG.md) for the rest.

## Features

- **USB disks only.** The list comes from a bus-filtered query on each platform, and the bus is checked again through the raw device handle before anything is written.
- **Layered refusals.** Boot disks, system disks, disks holding a protected location (`C:` and the Windows drive; `/`, `/boot`, `/home` and the rest on Linux), the disk the running system boots from, offline disks, read-only and write-protected disks, disks too small for a valid layout, and disks reporting an unsupported sector size are all refused rather than warned about.
- **GPT or MBR**, both written to the same 1 MiB-aligned layout, so switching the style never changes where the data starts or ends.
- **A summary dialog** listing the mount points and volume labels that are about to disappear, gated behind an acknowledgement checkbox, with an extra warning above 128 GB.
- **Revalidation at every step**, including a final identity check on the open handle immediately before the first destructive write.
- **Step-by-step progress**, with an activity log written to disk for when a step fails.
- **Light, dark and system themes**, remembered along with the window geometry and the layout choice.

## Download

Grab the latest build from the [Releases page](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest).

### Windows

1. Download `USB-Restoration-Tool-<version>.zip`.
2. Verify it against `SHA256SUMS.txt` in the bundle if you want to check it arrived intact.
3. Extract it anywhere and run `usb-restoration-tool.exe`.
4. Approve the Windows Administrator prompt. Raw disk access is not possible without it.

Keep the DLLs and plugin folders next to the executable. If Windows reports missing runtime DLLs, install the latest [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Releases can be distributed unsigned or Authenticode-signed. Unsigned builds may trigger SmartScreen warnings; see [docs/signing.md](docs/signing.md).

### Linux (AppImage)

1. Download `USB-Restoration-Tool-<version>-x86_64.AppImage`.
2. Make it executable and run it as root — raw disk access needs it:

```bash
chmod +x USB-Restoration-Tool-*-x86_64.AppImage
sudo ./USB-Restoration-Tool-*-x86_64.AppImage
```

Formatting uses the `mkfs.exfat` already on your machine, because a filesystem tool has to match the kernel it is writing for. Install it first if it is missing:

| Distro | Package |
|--------|---------|
| Debian/Ubuntu | `exfatprogs` |
| Fedora | `exfatprogs` |
| Arch | `exfatprogs` |

On systems without FUSE2 (some Ubuntu 24.04+ setups), install `libfuse2` or extract and run manually:

```bash
./USB-Restoration-Tool-*-x86_64.AppImage --appimage-extract
sudo ./squashfs-root/AppRun
```

## How To Use

1. Plug in the USB drive and start the app. Detected USB disks are listed with their size, current partition style, and where they are mounted.
2. Select the disk. The **Selected disk** card shows what the system reports about it, and the status badge says whether it can be restored — or why it cannot.
3. Pick **GPT** or **MBR** under **Layout**. GPT unless you know the device that has to read the stick predates it.
4. Choose **Restore USB**, tick the acknowledgement in the dialog, and confirm.

The restore takes a few seconds on a typical stick. **Cancel** stops it while it is still reversible; once the first sector has been rewritten it runs to completion, because a half-written partition table is worse than a finished one.

Afterwards the drive is one exFAT volume labelled `USB`. Windows assigns it a drive letter; on Linux mounting is left to the desktop, so that the volume belongs to you rather than to root.

### When something goes wrong

Every step is recorded. **Activity** in the header shows this session, and **Open log file** opens the log itself, which lives next to the executable:

```text
usb-restoration-tool.log
```

The most common failure is a mount that cannot be released because a file browser, antivirus scanner, or indexing service still has it open. On Linux this shows up as an exclusive-open failure naming the device. Close anything looking at the drive and try again.

## Safety Model

Nothing here is a warning that can be clicked through; each one is a refusal.

| Check | Refused when |
|-------|--------------|
| Bus type | The disk is not on the USB bus, per the system's own enumeration **and** per the raw device handle |
| Boot / system | The system marks the disk as either |
| Protected location | The disk holds `C:` or the Windows drive; `/`, `/boot`, `/home`, `/usr`, `/var` or the rest on Linux |
| System disk | The disk is the one a protected filesystem is mounted from |
| State | The disk is offline, read-only, or write-protected |
| Geometry | Under 8 MB, an unsupported sector size, or too small for a valid layout |
| Identity | The device path, size, sector size, serial number, unique ID, or model changed between selection and restore |
| Acknowledgement | The summary dialog's checkbox has not been ticked |

The summary dialog and the identity check are separate gates, and the last is repeated on the open handle immediately before the first write. See [SECURITY.md](SECURITY.md).

## Build From Source

Prerequisites:

- CMake 3.21 or newer.
- Qt 6.4 or newer with the `Core` and `Widgets` components, plus `Test` to run the C++ tests.
- A C++20 compiler: MSVC (Visual Studio 2022+) on Windows, or GCC/Clang on Linux.

Point CMake at your Qt kit, either as an environment variable or on the command line:

```powershell
# Windows (PowerShell)
$env:CMAKE_PREFIX_PATH = "C:\Qt\6.11.1\msvc2022_64"
```

```bash
# Linux
export CMAKE_PREFIX_PATH=/path/to/Qt/6.x/gcc_64
```

On Windows, if Qt is installed under `C:\Qt` and neither `CMAKE_PREFIX_PATH` nor `Qt6_DIR` is set, the build locates the newest MSVC kit itself.

### Build with presets

```bash
cmake --preset windows   # use "linux" on Linux
cmake --build --preset windows
ctest --preset windows
```

`windeployqt` runs post-build on Windows, so the Qt runtime DLLs and plugins land beside the executable. Output:

- Windows: `build\Release\usb-restoration-tool.exe`
- Linux: `build/usb-restoration-tool`

### Build without presets

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

Configure with `-DUSBRESTORE_BUILD_TESTS=OFF` to build just the app.

Linux development packages by distro:

| Distro | Packages |
|--------|----------|
| Ubuntu/Debian | `qt6-base-dev`, `cmake`, `g++`, `libgl1-mesa-dev`, `libxkbcommon-dev`, `exfatprogs` |
| Fedora | `qt6-qtbase-devel`, `cmake`, `gcc-c++`, `mesa-libGL-devel`, `libxkbcommon-devel`, `exfatprogs` |
| Arch | `qt6-base`, `cmake`, `gcc`, `mesa`, `libxkbcommon`, `exfatprogs` |

### Packaging

```powershell
# Windows
./scripts/build-standalone.ps1
```

Builds Release, exports the executable with the full Qt runtime to `standalone\USB-Restoration-Tool`, writes `SHA256SUMS.txt` covering every bundled file, and zips it. Add `-Sign -CertificateThumbprint <thumbprint>` to Authenticode-sign, and `-RequireSignature` for a pipeline that must fail on unsigned output — see [docs/signing.md](docs/signing.md).

```bash
# Linux
./scripts/build-appimage.sh
```

Produces `standalone/USB-Restoration-Tool-<version>-x86_64.AppImage` and a matching `.sha256`. Needs `curl` and `libfuse2`. The `standalone/` folder is regenerated on each run and is not tracked in git.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

The unit tests cover `src/core` — the safety refusals, the target-identity comparison, the layout arithmetic, and the GPT and MBR bytes. That is deliberately the half of the codebase with no OS headers behind it, so the rules that decide whether a disk may be erased can be exercised without a disk.

The partition tables get a second, independent check. `src/core/partition_table.cpp` is the code the Linux backend uses to write GPT and MBR itself, and a wrong byte there is a disk that no longer mounts — so rather than only checking the bytes against the specification, they are handed to the tools that will have to read them:

```bash
cmake --build build --target usb-partition-dump
./scripts/verify-partition-tables.sh build
```

This writes what a restore would produce into sparse image files and asks `sgdisk` and `fdisk` whether they accept them — GPT at 512 and 4096 bytes per sector, MBR, and an oversized MBR layout that must be refused. It needs `gdisk` and `util-linux`, touches no real disk, and runs in CI on every push.

The platform backends drive real hardware and have no automated coverage. **Do not test a restore on a drive you are not willing to lose.**

## Project Layout

On Linux the whole app currently runs as root, which is more privilege than the
work needs. Splitting the privileged part into a polkit helper is planned for
1.3.0 — see [docs/polkit-helper.md](docs/polkit-helper.md).

```text
src/core      Safety policy, layout arithmetic, GPT/MBR serialisation — no OS headers
src/platform  The DiskService interface, the restore worker, the log
src/win       Windows backend: WMI, DeviceIoControl, drive letters
src/linux     Linux backend: sysfs, mountinfo, raw block I/O, mkfs.exfat
src/gui       Qt widgets, theme, settings
tests         Tests for src/core
tools         usb-partition-dump, used by the partition table verification
scripts       Packaging and verification
```

Adding a third platform means implementing `DiskService` and nothing else; the GUI and every safety rule are written against that interface.

## License

[MIT](LICENSE).

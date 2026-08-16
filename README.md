# USB Restoration Tool

[![Build](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml/badge.svg)](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/KaroqDave/USB-Restoration-Tool?label=release)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-blue)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

A desktop app that gives a USB drive back after an ISO writer has been over it.

Writing a Linux ISO to a USB stick leaves it in a state the system barely understands: a hybrid partition table, a read-only ISO9660 volume, a few hundred megabytes of a drive that used to hold 64 GB. This app restores the whole drive to **one partition labelled `USB`** — under either **GPT** or **MBR**, formatted **exFAT**, **FAT32**, **NTFS**, or (on Linux) **ext4**.

It drives each platform's storage stack directly: `DeviceIoControl` and the `MSFT_Storage` WMI classes on Windows, sysfs and raw block-device I/O on Linux. There is no `diskpart`, no PowerShell, and no shelling out to `sfdisk`.

Built with C++ and Qt 6.

> **Restoring erases every partition and file on the selected disk.** Only USB disks are listed, and boot, system, offline, and read-only disks are refused outright — but the drive you pick is erased completely.

## What's New in 1.4.0 (unreleased)

- **A filesystem selector in the Restore card.** exFAT is still the default. FAT32 and NTFS are offered on both platforms; Linux also offers ext4. The list comes from the platform backend, so the GUI does not hard-code the OS.
- **Partition type follows the filesystem.** FAT32 uses MBR type `0x0C` so BIOS-era devices recognise it. ext4 uses the GPT Linux filesystem GUID so Windows does not offer to format the stick as RAW.
- **Windows cannot Format FAT32 above 32 GB.** That is a Format API limit; the Restore button is blocked with that reason rather than failing after the disk has been wiped.
- **Linux looks up `mkfs` before the first write.** A missing `mkfs.vfat` no longer leaves a blank partition table. The helper protocol is version 2 and carries `--filesystem` as another claim it may only refuse on.

> **Neither Linux path has been tested on real hardware.** That was already true of 1.3.0. Use a drive you can afford to lose. The Windows path has been verified on hardware for the original exFAT restore; FAT32 and NTFS on Windows, and every Linux filesystem, still need a disposable stick.

### Previously, in 1.3.0

- **The Linux app no longer runs as root.** An installed build runs the application as you, and hands the restore to a separate `usb-restoration-helper` that holds privilege for the duration of one restore and exits. Your desktop asks for the password when you press **Restore**, through polkit, the same way every other privileged action on the system does. It also means the app is launchable from the applications menu, and that its settings and log belong to you rather than to root.
- **The helper links Qt Core and nothing else.** Running the GUI as root meant every Qt plugin it loaded — widgets, theming, SVG and image formats, input methods — ran as root too, for the sake of the few hundred lines that actually needed it. None of that is loaded in the privileged process any more.
- **The helper trusts nothing it is told.** It re-enumerates the disk from sysfs, rebuilds its own list of protected devices and mount points, and re-runs every safety rule before anything is opened. The arguments it receives are claims it can only refuse on — notably, a caller cannot assert that its target is on the USB bus. That is what makes the boundary worth having: any local process can start the helper, and it decides for itself.
- **The AppImage is unchanged and still runs as root.** polkit reads its policies from system directories, which a self-contained bundle cannot register one in. Prefer an installed build where your distribution offers it; see [Linux](#linux).
- **Windows is untouched.** UAC already gates the process at launch and there is no equivalent worth building — splitting it would mean a service or a COM elevation moniker, both of which add more attack surface than they remove.

> **Neither Linux path has been tested on real hardware.** No restore has ever run against a physical USB stick on Linux — that was already true of 1.1.0 and is still true — and the polkit round trip added here has not run even once, because the environment it was written in has no polkit. Both compile clean and pass the test suite, and the rules that stop the wrong disk being touched are the well-covered part. The sequence that erases the right one is not. Use a drive you can afford to lose. The Windows path has been verified on hardware.

### Previously, in 1.1.0

- **Linux support.** A native backend built on sysfs, `/proc/self/mountinfo`, and raw block-device I/O, shipped as a portable AppImage. It enumerates USB disks, refuses the ones the running system depends on, unmounts what it is about to erase, writes the partition table itself, and hands the new partition to `mkfs.exfat`. Run it with `sudo`.
- **GPT or MBR.** A layout selector in the Restore card, defaulting to GPT. MBR is there for the devices that refuse to read a GPT stick at all: BIOS-era PCs, car stereos, older TVs and set-top boxes. The choice is remembered between runs.
- **The activity log moved into its own window,** behind an **Activity** button in the header. The main window is about a third shorter as a result and fits on a laptop screen without scrolling.
- **The typed confirmation phrase is gone.** In its place, the summary dialog that already named the disk now carries a checkbox to acknowledge what is about to happen. One gate instead of two, on the screen that actually describes the consequence.
- **The log lives beside the executable,** so a portable copy carries its own history and leaves nothing behind on the machines it is plugged into. It falls back to per-user application data only when the application folder is read-only, such as an AppImage's mount or an install under Program Files.
- **The partition tables are verified against the tools that read them.** `scripts/verify-partition-tables.sh` writes what a restore would produce into a sparse image and hands it to `sgdisk` and `fdisk`; CI runs it on every push. See [Testing](#testing).

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
- **exFAT, FAT32 or NTFS**, plus ext4 on Linux. The choice is remembered between runs. Partition type bytes follow the filesystem.
- **A summary dialog** listing the mount points and volume labels that are about to disappear, gated behind an acknowledgement checkbox, with an extra warning above 128 GB.
- **Revalidation at every step**, including a final identity check on the open handle immediately before the first destructive write.
- **Step-by-step progress**, with an activity log written to disk for when a step fails.
- **Light, dark and system themes**, remembered along with the window geometry, the layout choice, and the filesystem.

## Download

Grab the latest build from the [Releases page](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest).

### Windows

1. Download `USB-Restoration-Tool-<version>.zip`.
2. Verify it against `SHA256SUMS.txt` in the bundle if you want to check it arrived intact.
3. Extract it anywhere and run `usb-restoration-tool.exe`.
4. Approve the Windows Administrator prompt. Raw disk access is not possible without it.

Keep the DLLs and plugin folders next to the executable. If Windows reports missing runtime DLLs, install the latest [Microsoft Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

Releases can be distributed unsigned or Authenticode-signed. Unsigned builds may trigger SmartScreen warnings; see [docs/signing.md](docs/signing.md).

### Linux

There are two ways to run it, and they differ in how much of the app holds
privilege.

**Installed** — from a distribution package, or built and installed from source.
The application itself runs as you, and only a small non-GUI helper
(`usb-restoration-helper`) runs as root, for the duration of one restore. Your
desktop asks for the password when you press Restore. Launch it from the
applications menu or just:

```bash
usb-restoration-tool
```

**AppImage** — self-contained and portable, and the whole application runs as
root. polkit reads its policies from system directories, which a bundle that
installs nothing cannot write to, so the AppImage keeps the original behaviour:

```bash
chmod +x USB-Restoration-Tool-*-x86_64.AppImage
sudo ./USB-Restoration-Tool-*-x86_64.AppImage
```

Prefer the installed form where your distribution offers it. Running a Qt
application as root means every plugin it loads runs as root too, for the sake
of a few hundred lines that actually need it — see
[docs/polkit-helper.md](docs/polkit-helper.md) for what the split does about
that.

Formatting uses the `mkfs` already on your machine, because a filesystem tool has to match the kernel it is writing for. Install the tool for the filesystem you pick if it is missing:

| Filesystem | Debian/Ubuntu | Fedora | Arch |
|------------|---------------|--------|------|
| exFAT | `exfatprogs` | `exfatprogs` | `exfatprogs` |
| FAT32 | `dosfstools` | `dosfstools` | `dosfstools` |
| NTFS | `ntfs-3g` | `ntfsprogs` | `ntfs-3g` |
| ext4 | `e2fsprogs` | `e2fsprogs` | `e2fsprogs` |

On systems without FUSE2 (some Ubuntu 24.04+ setups), install `libfuse2` or extract and run manually:

```bash
./USB-Restoration-Tool-*-x86_64.AppImage --appimage-extract
sudo ./squashfs-root/AppRun
```

## How To Use

1. Plug in the USB drive and start the app. Detected USB disks are listed with their size, current partition style, and where they are mounted.
2. Select the disk. The **Selected disk** card shows what the system reports about it, and the status badge says whether it can be restored — or why it cannot.
3. Pick **GPT** or **MBR** under **Layout**, and **exFAT**, **FAT32**, **NTFS**, or (on Linux) **ext4** under **Filesystem**. GPT and exFAT unless you know the device that has to read the stick needs something else.
4. Choose **Restore USB**, tick the acknowledgement in the dialog, and confirm.

The restore takes a few seconds on a typical stick. **Cancel** stops it while it is still reversible; once the first sector has been rewritten it runs to completion, because a half-written partition table is worse than a finished one.

Afterwards the drive is one volume labelled `USB`. Windows assigns it a drive letter; on Linux mounting is left to the desktop, so that the volume belongs to you rather than to root.

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
| Ubuntu/Debian | `qt6-base-dev`, `cmake`, `g++`, `libgl1-mesa-dev`, `libxkbcommon-dev`, `exfatprogs`, `dosfstools` |
| Fedora | `qt6-qtbase-devel`, `cmake`, `gcc-c++`, `mesa-libGL-devel`, `libxkbcommon-devel`, `exfatprogs`, `dosfstools` |
| Arch | `qt6-base`, `cmake`, `gcc`, `mesa`, `libxkbcommon`, `exfatprogs`, `dosfstools` |

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

This writes what a restore would produce into sparse image files and asks `sgdisk` and `fdisk` whether they accept them — GPT at 512 and 4096 bytes per sector, MBR, FAT32 MBR, ext4 GPT, and an oversized MBR layout that must be refused. It needs `gdisk` and `util-linux`, touches no real disk, and runs in CI on every push.

The platform backends drive real hardware and have no automated coverage. **Do not test a restore on a drive you are not willing to lose.**

## Project Layout

On Linux an installed build ships two binaries: the GUI, which holds no
privilege, and `usb-restoration-helper`, which holds it for one restore and
decides for itself whether that restore may happen. See
[docs/polkit-helper.md](docs/polkit-helper.md). The AppImage keeps the older
arrangement where the whole application runs as root.

```text
src/core      Safety policy, layout arithmetic, GPT/MBR serialisation, the
              GUI-to-helper protocol — no OS headers
src/platform  The DiskService interface, the restore worker, the log
src/win       Windows backend: WMI, DeviceIoControl, drive letters
src/linux     Linux backend: sysfs, mountinfo, raw block I/O, mkfs
src/helper    usb-restoration-helper: the privileged Linux restore, Qt Core only
src/gui       Qt widgets, theme, settings
tests         Tests for src/core
tools         usb-partition-dump, used by the partition table verification
scripts       Packaging and verification
```

The helper's installed location is baked into both the GUI and the polkit
action, so it is fixed at **configure** time. Package with
`-DCMAKE_INSTALL_PREFIX=/usr` and relocate with `DESTDIR` when staging;
overriding the prefix at `cmake --install` time would move the binary without
moving the path that authorises it.

Adding a third platform means implementing `DiskService` and nothing else; the GUI and every safety rule are written against that interface.

## License

[MIT](LICENSE).

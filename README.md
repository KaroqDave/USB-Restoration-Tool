# USB Restoration Tool

[![Build](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml/badge.svg)](https://github.com/KaroqDave/USB-Restoration-Tool/actions/workflows/build.yml)
[![Latest release](https://img.shields.io/github/v/release/KaroqDave/USB-Restoration-Tool?label=release)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![Platforms](https://img.shields.io/badge/platforms-Windows%20%7C%20Linux-blue)](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest)
[![License](https://img.shields.io/badge/license-MIT-green)](LICENSE)

Restore an ISO-written or unusable USB drive to one partition labelled `USB`.
Choose **GPT or MBR** and **exFAT, FAT32 or NTFS**. Linux also supports **ext4**.

Built with Qt 6 for Windows and Linux. Tested across multiple devices and systems.

> **Restoring erases every partition and file on the selected drive.** Check the
> model and capacity before confirming. Only USB disks are eligible; system,
> protected and read-only disks are refused.

## Download and run

Get a build from [Releases](https://github.com/KaroqDave/USB-Restoration-Tool/releases/latest).

### Windows

1. Download and extract `USB-Restoration-Tool-<version>.zip`.
2. Run `usb-restoration-tool.exe` and approve the Administrator prompt.

Keep the bundled DLLs and plugin folders beside the executable. `SHA256SUMS.txt`
contains checksums for the bundle. Unsigned builds may show a SmartScreen warning.
If runtime DLLs are missing, install the
[Visual C++ Redistributable (x64)](https://aka.ms/vs/17/release/vc_redist.x64.exe).

### Linux

On Ubuntu 24.04 or later and Debian 13 or later, install the `.deb`. It pulls
in the formatters and pkexec:

```bash
sudo apt install ./usb-restoration-tool_*_amd64.deb
```

An installed build runs the GUI as your normal user and asks for authentication
when restoring. Launch it from the applications menu or run `usb-restoration-tool`.
See [Development](docs/development.md) to build and install from source.

The portable AppImage runs the whole application as root:

```bash
chmod +x USB-Restoration-Tool-*-x86_64.AppImage
sudo ./USB-Restoration-Tool-*-x86_64.AppImage
```

With the AppImage or a source build, install the formatter for the filesystem you want:

| Filesystem | Debian / Ubuntu | Fedora | Arch |
| --- | --- | --- | --- |
| exFAT | `exfatprogs` | `exfatprogs` | `exfatprogs` |
| FAT32 | `dosfstools` | `dosfstools` | `dosfstools` |
| NTFS | `ntfs-3g` | `ntfsprogs` | `ntfs-3g` |
| ext4 | `e2fsprogs` | `e2fsprogs` | `e2fsprogs` |

If the AppImage cannot connect to your display, allow root access temporarily:

```bash
xhost +SI:localuser:root
sudo ./USB-Restoration-Tool-*-x86_64.AppImage
xhost -SI:localuser:root
```

If FUSE is unavailable, extract the AppImage and run it:

```bash
./USB-Restoration-Tool-*-x86_64.AppImage --appimage-extract
sudo ./squashfs-root/AppRun
```

## Restore a drive

1. Connect the USB drive and select it in the app.
2. Check its model, capacity and listed volumes.
3. Choose the partition style and filesystem. GPT and exFAT are the defaults.
4. Select **Restore USB**, read the summary, tick the acknowledgement and confirm.

**Cancel** stops the restore before destructive work begins. Existing volumes may
need remounting or drive-letter reassignment. Once destructive work starts, leave
the drive connected until the operation finishes.

The result is one volume labelled `USB`. Windows assigns a drive letter; Linux
leaves mounting to the desktop.

## Troubleshooting

Close file browsers and other programs using the drive if it cannot be unmounted
or opened. A stalled device may take time to respond; the app displays how long
it has been silent and guidance for the current stage.

**Activity** shows the session log. **Open log file** opens
`usb-restoration-tool.log`, stored beside the executable or in per-user
application data when that directory is not writable.

## What's New in 1.5.0

- Filesystem selection, with ext4 available on Linux.
- Filesystem names in the selected-disk panel.
- Progress feedback when a device stops responding.
- Accurate privilege badges and GNOME Wayland window decorations.
- More reliable cancellation during Linux authentication.
- FAT32 geometry checks and Linux ext4 ownership fixes.

See [CHANGELOG.md](CHANGELOG.md) for release history.

## Development and safety

- [Development](docs/development.md): build, test, package and project structure.
- [Linux helper](docs/polkit-helper.md): privilege separation and protocol.
- [Tasks](docs/TODO.md): remaining work.
- [Security policy](SECURITY.md): safety checks and vulnerability reporting.

## License

[MIT](LICENSE).

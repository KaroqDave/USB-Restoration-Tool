# Security Policy

USB Restoration Tool erases disks and always runs as Administrator. Both facts
shape what counts as a security issue here: anything that could cause the tool
to write to a disk other than the one the user confirmed, and anything that
could let code run with the elevation the tool holds.

## Supported Versions

| Version | Supported |
| ------- | --------- |
| 1.0.x   | Yes       |
| 0.1.x   | No        |

## Safety Model

The tool never writes to a disk that has not passed all of the following. Each
is a refusal, not a warning.

| Check | Refused when |
|-------|--------------|
| Bus type | The disk is not on the USB bus, per WMI **and** per the raw device handle |
| Boot / system | Windows marks the disk as either |
| Drive letters | The disk holds `C:`, the Windows drive, or the drive the app is running from |
| State | The disk is offline, read-only, or reports write protection |
| Geometry | Under 8 MB, an unsupported sector size, or too small for a valid GPT layout |
| Identity | Disk number, size, sector size, serial number, unique ID, device path, or model changed since selection |
| Confirmation | `RESTORE DISK <number>` for that exact disk has not been typed, or the summary dialog was not accepted |

### Time-of-check to time-of-use

A disk number is not a stable name for a device: Windows reuses one as soon as
a device is unplugged. Everything decided before the drive is opened therefore
describes a disk that `\\.\PhysicalDriveN` may no longer name.

The tool closes that gap by re-reading identity from the open handle itself —
`IOCTL_STORAGE_GET_DEVICE_NUMBER`, `IOCTL_STORAGE_QUERY_PROPERTY`,
`IOCTL_DISK_GET_LENGTH_INFO`, `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` and
`IOCTL_DISK_IS_WRITABLE` — immediately after opening it, and once more
immediately before the first destructive write. The handle stays open for the
whole restore, so every raw operation acts on the device object that passed
those checks.

Drive-letter ownership is decided the same way, through
`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` on a handle opened with no access
rights. A write handle is only opened for a letter that has already proved to
sit on the target disk, and the extent check is repeated on that handle before
it is locked and dismounted.

### Elevated-process hardening

- The DLL search order is narrowed with `SetDefaultDllDirectories` before any
  other DLL loads, removing the current directory and `%PATH%` — the two
  locations an unprivileged user can most easily influence.
- COM is initialised process-wide with `RPC_C_AUTHN_LEVEL_PKT_PRIVACY`, and
  each WMI proxy is set to the same level.
- The manifest requests elevation up front rather than letting the tool fail
  partway through a disk operation. There is no self-elevation path.
- Release builds are compiled with `/sdl /guard:cf` and linked with
  `/guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`.
- The app takes no command-line arguments. There is no way to script a restore,
  and that is deliberate.

### What the log contains

`%LOCALAPPDATA%\KaroqDave\USB Restoration Tool\usb-restoration-tool.log`
records the model, size, bus, sector size, drive letters and volume labels of
the target disk, plus each step and any Windows error text. It does not record
serial numbers or unique IDs. Redact anything you would rather not share before
attaching it to a report.

## Reporting a Vulnerability

Open a [GitHub security advisory](https://github.com/KaroqDave/USB-Restoration-Tool/security/advisories/new)
with:

- Windows version and tool version.
- The disk layout and USB model, if relevant.
- The log file, with anything private redacted.

Please do not publish a working bypass of the destructive-action gates before
there is a fixed release.

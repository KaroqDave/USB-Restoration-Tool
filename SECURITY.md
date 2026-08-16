# Security Policy

USB Restoration Tool erases disks and always runs with elevated permission —
Administrator on Windows, root on Linux. Both facts shape what counts as a
security issue here: anything that could cause the tool to write to a disk
other than the one the user confirmed, and anything that could let code run
with the privilege the tool holds.

## Supported Versions

Only the current release line receives security fixes.

| Version | Supported |
| ------- | --------- |
| 1.3.x   | Yes       |
| 1.1.x   | No        |
| 1.0.x   | No        |
| 0.1.x   | No        |

## Safety Model

The tool never writes to a disk that has not passed all of the following. Each
is a refusal, not a warning.

| Check | Refused when |
|-------|--------------|
| Bus type | The disk is not on the USB bus, per the system enumeration **and** per the raw device handle |
| Boot / system | The system marks the disk as either |
| Protected location | The disk holds `C:`, the Windows drive, or the drive the app runs from; `/`, `/boot`, `/boot/efi`, `/efi`, `/etc`, `/home`, `/nix`, `/opt`, `/srv`, `/usr` or `/var` on Linux |
| System disk | The disk is the one a protected filesystem is mounted from |
| State | The disk is offline, read-only, or reports write protection |
| Geometry | Under 8 MB, an unsupported sector size, or too small for a valid layout |
| Identity | Device path, size, sector size, serial number, unique ID, or model changed since selection |
| Acknowledgement | The summary dialog naming that disk was not accepted with its checkbox ticked |

A mount point counts as protected when it *is* one of those paths or when it
*contains* one, so a disk mounted at `/` is refused for holding `/boot` whether
or not `/boot` is a separate mount.

### Time-of-check to time-of-use

A device path is not a stable name for a device. Windows reuses a disk number
as soon as a device is unplugged, and a Linux device node can be re-created
pointing elsewhere. Everything decided before the disk is opened therefore
describes a disk the path may no longer name.

The tool closes that gap by re-reading identity from the open handle itself:

- **Windows** — `IOCTL_STORAGE_GET_DEVICE_NUMBER`,
  `IOCTL_STORAGE_QUERY_PROPERTY`, `IOCTL_DISK_GET_LENGTH_INFO`,
  `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX` and `IOCTL_DISK_IS_WRITABLE`.
- **Linux** — `fstat` against the `major:minor` sysfs reports for that disk,
  `BLKGETSIZE64`, `BLKSSZGET`, the `ro` attribute, and the bus path re-read
  from sysfs.

Both run immediately after opening and again immediately before the first
destructive write, and the descriptor stays open for the whole restore, so
every raw operation acts on the device that passed those checks.

On Linux the exclusive open is itself a gate: `O_EXCL` on a block device is
refused by the kernel while any partition of it is mounted or otherwise
claimed, so reaching the write means nothing is live underneath it.

On Windows, drive-letter ownership is decided through
`IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` on a handle opened with no access
rights. A write handle is only opened for a letter that has already proved to
sit on the target disk, and the extent check is repeated on that handle before
it is locked and dismounted.

### Elevated-process hardening

Windows:

- The DLL search order is narrowed with `SetDefaultDllDirectories` before any
  other DLL loads, removing the current directory and `%PATH%` — the two
  locations an unprivileged user can most easily influence.
- COM is initialised process-wide with `RPC_C_AUTHN_LEVEL_PKT_PRIVACY`, and
  each WMI proxy is set to the same level.
- The manifest requests elevation up front rather than letting the tool fail
  partway through a disk operation. There is no self-elevation path.
- Release builds are compiled with `/sdl /guard:cf` and linked with
  `/guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`.
- The log and the project link are opened at the logged-in user's integrity
  level rather than launching a browser as Administrator.

Linux:

- `PR_SET_DUMPABLE` is turned off, so a crash cannot leave a core dump of a
  root process where the invoking session can read it.
- The umask is tightened at startup, so files the app creates are not group- or
  world-readable.
- `mkfs` is resolved from a fixed list of system directories rather than
  through `$PATH`, which a caller reaching root through `sudo` may control. It
  is executed with an argument list, never through a shell.
- Builds are compiled with `-D_FORTIFY_SOURCE=2 -fstack-protector-strong` and
  linked with `-pie -Wl,-z,relro,-z,now,-z,noexecstack`.

Both:

- Every count and offset a driver reports is bounded against the bytes actually
  returned before it is used to index a buffer.
- The app takes no command-line arguments. There is no way to script a restore,
  and that is deliberate.

### What the log contains

The log lives beside the executable as `usb-restoration-tool.log`, falling back
to per-user application data when that directory is not writable. It records
the model, size, bus, sector size, mount points and volume labels of the target
disk, plus each step and any system error text. It does not record serial
numbers or unique IDs. Redact anything you would rather not share before
attaching it to a report.

## Reporting a Vulnerability

Open a [GitHub security advisory](https://github.com/KaroqDave/USB-Restoration-Tool/security/advisories/new)
with:

- Windows version and tool version.
- The disk layout and USB model, if relevant.
- The log file, with anything private redacted.

Please do not publish a working bypass of the destructive-action gates before
there is a fixed release.

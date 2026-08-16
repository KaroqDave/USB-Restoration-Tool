---
tags:
  - safety
---

# Safety

Every check is a **refusal**, not a warning that can be clicked through. Published policy lives in `SECURITY.md`; this note is the working summary.

## Refusals

| Check | Refused when |
|---|---|
| Bus type | Not on the USB bus, per system enumeration **and** per the raw device handle |
| Boot / system | The system marks the disk as either |
| Protected location | Holds `C:` / the Windows drive / the drive the app runs from; or `/`, `/boot`, `/boot/efi`, `/efi`, `/etc`, `/home`, `/nix`, `/opt`, `/srv`, `/usr`, `/var` on Linux |
| System disk | The disk a protected filesystem is mounted from |
| State | Offline, read-only, or write-protected |
| Geometry | Under 8 MB, unsupported sector size, or too small for a valid layout |
| Identity | Path, size, sector size, serial, unique ID, or model changed since selection |
| Acknowledgement | Summary dialog checkbox not ticked |

A POSIX mount point matches when it **is** a protected path or **contains** one, so a disk mounted at `/` is refused for holding `/boot` even if `/boot` is not a separate mount.

Above **128 GB**, the summary dialog adds an extra warning. That is still not a bypassable gate — it is additional copy. The disk remains USB-only.

## Time-of-check to time-of-use

A device path is not a stable name. Windows reuses disk numbers as soon as a stick is unplugged; a Linux node can be recreated pointing elsewhere. Everything decided before open describes a disk the path may no longer name.

Identity is therefore re-read **from the open handle**, immediately after open and again immediately before the first destructive write. The descriptor stays open for the whole restore.

- **Windows** — `IOCTL_STORAGE_GET_DEVICE_NUMBER`, `IOCTL_STORAGE_QUERY_PROPERTY`, `IOCTL_DISK_GET_LENGTH_INFO`, `IOCTL_DISK_GET_DRIVE_GEOMETRY_EX`, `IOCTL_DISK_IS_WRITABLE`
- **Linux** — `fstat` vs sysfs `major:minor`, `BLKGETSIZE64`, `BLKSSZGET`, `ro`, bus path re-read from sysfs. `O_EXCL` is itself a gate: the kernel refuses it while any partition is mounted or claimed.

## Two gates, two threats (Linux helper)

The helper trusts nothing it is told. Arguments are claims.

- `isSafeRestoreTarget()` against what sysfs says **now** is the defence against a **malicious** caller. Any local process can start the helper.
- `isSameRestoreTarget()` is the defence against an **honest** caller whose disk changed under the polkit prompt.

The caller **cannot assert bus type**. A `DiskInfo` assembled from arguments alone fails `isSafeRestoreTarget()` for that reason. Do not add a field that lets an argument decide USB-ness.

## Do not

- Bypass the acknowledgement dialog or the identity check
- Add a GUI command-line flag that starts a restore
- Widen the bus-type check
- Let helper arguments decide facts instead of refuse on them

## Related

- [[Architecture]]
- [[Linux]]
- [[Windows]]
- [[Decisions]]
- [[TODO]]

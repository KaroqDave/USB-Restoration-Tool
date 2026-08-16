---
tags:
  - windows
  - platform
---

# Windows

The Windows backend lives in `src/win/`. The process is elevated at launch via the manifest (UAC). There is no self-elevation path and no helper split — that is deliberate. See [[Decisions]].

## How it talks to the disk

- **Enumeration** — bus-filtered query; USB only
- **WMI** — `MSFT_Storage` classes for layout and format. A return of `4096` means "queued as a job", not "done"
- **Raw I/O** — `DeviceIoControl` on `\\.\PhysicalDriveN`
- **Volumes** — drive-letter ownership via `IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS` on a handle opened with no access rights. A write handle is opened only for a letter that already proved to sit on the target disk; the extent check is repeated before lock and dismount

## Identity on the open handle

Re-read before the first write: device number, bus type, byte length, sector size, serial, writability. Disk numbers are reused as soon as a device is unplugged, so the number chosen in the UI is not enough.

A past bug (fixed in 1.0.0): every 16-bit property Windows reports was being read as zero, including the bus type the USB check compared against.

## Hardening

- `SetDefaultDllDirectories` before any other DLL load — current directory and `%PATH%` are out of the search order
- COM at `RPC_C_AUTHN_LEVEL_PKT_PRIVACY`, including each WMI proxy
- Release: `/sdl /guard:cf`, linked `/guard:cf /CETCOMPAT /DYNAMICBASE /NXCOMPAT /HIGHENTROPYVA`
- Log and project link opened at the logged-in user's integrity level, not as Administrator

## Status

The Windows restore path **has been verified on hardware**. Which sticks and layouts is not recorded — capture that in [[Hardware Testing]].

Signing is optional. See `docs/signing.md` and [[Releases]].

## Related

- [[Safety]]
- [[Architecture]]
- [[Hardware Testing]]

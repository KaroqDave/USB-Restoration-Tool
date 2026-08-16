---
tags:
  - architecture
---

# Architecture

USB Restoration Tool is split so that **safety policy is testable without a disk**, and **each OS is a backend behind one interface**.

```text
src/core      DiskInfo, safety refusals, layout arithmetic, GPT/MBR bytes,
              GUI-to-helper protocol. No OS headers. This is what the tests cover.
src/platform  DiskService, RestoreWorker, logger, startup
src/win       WMI, DeviceIoControl on \\.\PhysicalDriveN, volumes
src/linux     sysfs, mountinfo, raw block I/O, mkfs
              enumerate (read-only) vs restore (privileged) are separate units
src/helper    usb-restoration-helper — Linux only, Qt Core, no widgets
src/gui       Qt widgets, theme, settings
tests         QtTest for src/core
tools         usb-partition-dump, used by scripts/verify-partition-tables.sh
```

Adding a third platform means implementing `DiskService` and nothing else. If a change needs the GUI or `src/core` to know which OS it is on, the abstraction is in the wrong place.

## `DiskService`

`src/platform/disk_service.h` is the seam:

- `listUsbDisks` / `refreshDisk` — enumeration
- `restoreGuard` — mount points and devices the running system depends on
- `restore` — the destructive sequence, reporting progress through `RestoreReporter`
- `isPrivileged` — whether a restore can happen at all (elevated on Windows; root **or** reachable helper on Linux)

The GUI and every safety rule are written against this.

## Core vs backend

New refusals belong in `src/core/safety.*`, not inline in a backend. Platform code supplies facts (`RestoreGuard`, bus type, identity from the open handle) and carries out I/O. It does not invent policy.

`src/core/restore_protocol.*` is the GUI-to-helper line protocol. Its argument half is a **trust boundary**: the helper may only refuse on what it is told, never act on a claim it did not re-derive. See [[Linux]] and [[Safety]].

## Linux process split

An installed Linux build is two binaries:

| Binary | Privilege | Links |
|---|---|---|
| `usb-restoration-tool` | unprivileged | GUI, Qt Widgets |
| `usb-restoration-helper` | root for one restore, then exits | `src/core` + `src/linux` restore, Qt Core only |

The AppImage does not ship the helper and still runs everything as root. Details: [[Linux]], repo `docs/polkit-helper.md`.

## Related

- [[Safety]]
- [[Windows]]
- [[Linux]]
- [[Development]]
- [[Decisions]]

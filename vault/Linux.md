---
tags:
  - linux
  - platform
---

# Linux

The Linux backend lives in `src/linux/`, split so the helper can link the restore without the service:

| Unit | Role |
|---|---|
| `linux_enumerate.*` | Read-only: sysfs, `/proc/self/mountinfo`, partition-style probe |
| `linux_restore.*` | Privileged sequence: exclusive open, partition table, `mkfs` |
| `linux_disk_service.*` | Thin `DiskService` over both; picks in-process vs helper |
| `helper_client.*` | `pkexec` spawn, protocol parse |
| `src/helper/main.cpp` | `usb-restoration-helper` |

Read `docs/polkit-helper.md` before restructuring any of this.

## Two ways to run

**Installed** — GUI unprivileged; restore in `usb-restoration-helper` via `pkexec`. Desktop password prompt on Restore. Helper links Qt Core only. Arguments are claims; the helper re-enumerates, rebuilds its own `RestoreGuard`, and re-runs `isSafeRestoreTarget()` before anything is opened. Bus type is not accepted from the caller.

**AppImage** — whole application as root. polkit policies live in system directories; a bundle that installs nothing cannot register one. `USBRESTORE_INSTALL_HELPER=OFF` leaves the helper and policy out.

The helper's installed path is baked in at **configure** time (`CMAKE_INSTALL_PREFIX`). Relocate with `DESTDIR` when staging; do not override the prefix at `cmake --install` time.

## Restore mechanics

- Enumerate USB disks from sysfs
- Unmount what is about to be erased
- Open the block device `O_EXCL` (kernel-refused while mounted)
- Write GPT or MBR from `src/core/partition_table.cpp` (no `sfdisk`)
- Hand the new partition to `mkfs` (exFAT, FAT32, NTFS or ext4), resolved from a fixed list of system directories, never `$PATH`, never a shell

`detectPartitionStyle()` falls back to udev's `ID_PART_TABLE_TYPE` when the unprivileged GUI cannot open the node, so the list still shows GPT/MBR. That label is second-hand. **No safety rule reads `partitionStyle`.**

## Hardening

- `PR_SET_DUMPABLE` off
- Restrictive umask at startup
- `-D_FORTIFY_SOURCE=2 -fstack-protector-strong`, linked `-pie -Wl,-z,relro,-z,now,-z,noexecstack`

## Status

**Unverified on hardware.** No physical USB restore on Linux, ever. The pkexec round trip has not run even once. See [[TODO]] and [[Hardware Testing]].

## Related

- [[Safety]]
- [[Architecture]]
- [[Decisions]]
- [[Releases]]

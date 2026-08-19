---
tags:
  - testing
  - hardware
---

# Hardware Testing

The backends drive real hardware and have no automated coverage. **Never test a restore on a drive whose contents matter.**

Template for a new run: [[Templates/Hardware test]].

## What to cover

On each platform, and for both GPT and MBR:

1. An ISO-written stick (hybrid table, ISO9660, "a few hundred megabytes of a 64 GB drive")
2. A stick with several partitions
3. A stick with no partition table at all

Afterwards: one volume labelled `USB` in the filesystem that was chosen. Windows assigns a letter; Linux leaves mounting to the desktop.

Cancel: confirm it still stops while reversible, and that it does **not** stop once the first sector is rewritten.

## Windows

README: the Windows path has been verified on hardware. Details were not written down.

| Date | Model / size | Prior layout | Style | Result | Notes |
|---|---|---|---|---|---|
| | | | GPT | | |
| | | | MBR | | |
| | ISO-written | | | | |
| | Several partitions | | | | |
| | No table | | | | |

## Linux — installed (helper / polkit)

First exercised 2026-08-19: Ubuntu 26.04 (GNOME on Wayland, pkexec 127), build
installed to `/usr/local` (polkitd picked the action up from
`/usr/local/share/polkit-1/actions` — no copy to `/usr/share` was needed).

| Date | Distro | Model / size | Prior layout | Style | Result | Notes |
|---|---|---|---|---|---|---|
| 2026-08-19 | Ubuntu 26.04 | SanDisk 3.2Gen1 / 57.3 GB (61504880640 B, 512 B sectors) | ISO-written: Ubuntu 26.04 hybrid (iso9660 spanning the disk + vfat ESP + 300 K stub + ext4 "writable") | GPT | ✓ one exFAT volume "USB" at /dev/sda1 | pkexec prompt shown; mkfs.exfat detail lines arrived in the user log; finished in seconds; desktop automounted the result |
| 2026-08-19 | Ubuntu 26.04 | same stick | one GPT exFAT partition, mounted, ~18 GB of files | MBR | ✓ dos label, one type 0x07 partition, exFAT "USB" | first attempt failed *clean* at "Unmounting": the stick stalled on writeback (helper in D state ~4½ min, kernel logged "waiting for writeback completion for more than 278 seconds"), then the error was reported and the disk was untouched; retry succeeded in seconds |

Cancel:

- **During polkit authentication** — Cancel clicked in the GUI while the
  password dialog was up: "Restore cancelled. The disk was not changed."
  Disk verified untouched. This is the kill-race fix of 2026-08-19 on hardware.
- **At a checkpoint** — not reachable by hand: on a quiescent stick both
  reversible checkpoints pass in about a second. Exercised instead by running
  the helper directly with the token already on stdin
  (`echo cancel | sudo …/usb-restoration-helper --device /dev/sda …`):
  printed steps 1–2 only, exit code 3, disk unchanged.
- **After writing began** — a run with a pending automounter and 18 GB of
  files still finished in seconds, too fast to cancel by hand; it completed
  and was reported as complete, which is the documented contract.

Also confirmed: GUI runs unprivileged (ps shows the desktop user) with only
the helper as root; settings and log owned by the user, mode 0600; helper exits after one
restore; `auth_admin_keep` works (a second restore minutes later did not
re-prompt); mounting is left to the desktop, which automounts the new volume
immediately.

Still to cover on this path: a stick with several non-ISO partitions, a stick
with no partition table, the ISO-written → MBR variant, and the FAT32, NTFS
and ext4 formats.

## Linux — AppImage (run as root)

**Never exercised on a physical stick.**

| Date | Distro | Model / size | Prior layout | Style | Result | Notes |
|---|---|---|---|---|---|
| | | | | GPT | | |
| | | | | MBR | | |

## Related

- [[TODO]]
- [[Linux]]
- [[Windows]]
- [[Development]]

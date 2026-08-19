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
| 2026-08-20 | Ubuntu 26.04 | same stick | no partition table (`wipefs -a` on the whole disk) | GPT | ✓ GPT, one partition at sector 2048, exFAT "USB" | disk list showed "RAW / unknown · not mounted", contents "—", and still offered the disk; run completed without incident. Setup note: GNOME's *eject* powers the stick down (capacity drops to 0), so wipefs after eject silently wipes nothing — unmount without ejecting, then wipe |
| 2026-08-20 | Ubuntu 26.04 | same stick | three GPT partitions: stale exFAT "USB" signature (sfdisk left it in place), FAT32 "STUFF" mounted, one blank | MBR | ✓ dos label, one type 0x0c partition at sector 2048, FAT32 "USB" | covers "several partitions" and the FAT32 format in one run; the helper unmounted the mounted middle partition itself |
| 2026-08-20 | Ubuntu 26.04 | same stick | one MBR FAT32 partition | GPT | ✓ GPT, one Microsoft-Basic-Data partition at sector 2048, NTFS "USB" | NTFS format cell |
| 2026-08-20 | Ubuntu 26.04 | same stick | one GPT NTFS partition | MBR | ✓ dos label, one type 0x83 partition at sector 2048, ext4 "USB" | ext4 format cell — all four filesystems now exercised on this path |
| 2026-08-20 | Ubuntu 26.04 | same stick | ISO-written: CachyOS 2026.08 hybrid (dos table, iso9660 on the whole disk *and* as a 2.9 GB partition, vfat "ARCHISO_EFI") | MBR | ✓ dos label, one type 0x07 partition at sector 2048, exFAT "USB" | ISO-written → MBR variant; the whole-disk iso9660 signature was removed too. **Completes the matrix for this path.** |

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

The matrix in "What to cover" is complete for this path as of 2026-08-20:
all three prior-layout classes, both styles, all four filesystems, and all
three cancel behaviours.

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

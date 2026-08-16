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

**Never exercised.** Needs polkit, an installed build, a disposable stick.

| Date | Distro | Model / size | Prior layout | Style | Result | Notes |
|---|---|---|---|---|---|
| | | | | GPT | | pkexec prompt? |
| | | | | MBR | | |
| | | ISO-written | | | | |
| | | Several partitions | | | | |
| | | No table | | | | |

Also confirm: unprivileged GUI, settings/log owned by the user, helper exits after one restore, cancel-while-reversible.

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

---
tags:
  - decisions
---

# Decisions

Why the tool is shaped this way. Add a note when a choice is made that is not already here. Template: [[Templates/Decision]].

## USB only, never widen the bus check

The list is bus-filtered, and the bus is checked again on the open handle. Widening it (SCSI, NVMe, "removable") is how an external SSD full of backups gets erased. Large-disk copy at 128 GB is extra warning, not a substitute.

## No GUI command-line restore

The app takes no arguments that start a restore. That is deliberate. `usb-restoration-helper` is the one exception, and it earns it by re-deriving every fact rather than accepting one.

## Helper arguments are claims, not facts

Any local process can start the helper. `isSafeRestoreTarget()` against a freshly enumerated disk is the only defence against a malicious caller. The one field deliberately not sent is bus type.

## AppImage stays root

polkit reads policies from system directories. A self-contained bundle cannot register one. Privilege separation ships with a real install; the AppImage keeps the original behaviour and says so.

## No Windows helper

UAC already gates the process at launch. A service or COM elevation moniker would add more attack surface than it removes.

## Safety policy lives in `src/core`

No OS headers in `src/core`. New refusals go there, with a test, including the case that used to be allowed. Backends supply facts and perform I/O.

## One interface per platform

Adding a platform means implementing `DiskService` and nothing else. If the GUI or `src/core` needs to know which OS it is on, the abstraction is in the wrong place.

## Same layout for GPT and MBR

1 MiB alignment at both ends. Switching style never moves where data starts or ends. The MBR reservation costs a megabyte and matches what `sgdisk -v` expects. MBR carries a real disk signature; Windows tells disks apart by it.

## Cancel only while reversible

Once the first sector has been rewritten, the restore runs to completion. A half-written partition table is worse than a finished one. If the Linux GUI dies mid-helper-restore, end of stdin is not a cancel.

## Partition tables checked by the tools that read them

Bytes that satisfy the spec can still be rejected by `sgdisk` and `fdisk`. CI runs `scripts/verify-partition-tables.sh` on every push.

## Related

- [[Safety]]
- [[Architecture]]
- [[Linux]]
- [[Windows]]

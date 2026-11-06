# Tasks

## Next

- Fix the flaky Linux loop test `rereadsPartitionTableAfterFormatting`. CI
  failed it on 2026-10-24 (`4096-mkfs-failed`, the partition start check) and on
  2026-11-06 (`4096-formatted`: "/dev/loop0p1 did not appear as a partition of
  /dev/loop0"); both passed on rerun. The test opens `p1` as soon as the node
  exists, then checks `/sys/block/<disk>/<partition>` once, which can race the
  kernel's partition re-read, or a node left over from the previous data row.
  Wait for the sysfs entry as well, then decide whether
  `performLinuxRestore`'s single `verifyPartitionOf()` call needs the same
  wait on slow media.
- On Windows, run `./scripts/build-standalone.ps1` with `pwsh` from an unelevated
  shell. CI cannot cover this: its runners are elevated. Check that it writes
  `USB-Restoration-Tool-<version>.zip.sha256` beside the zip.
- On Windows, configure, build and test with the `windows` preset from a local
  Developer PowerShell. CI's `build-windows-preset` job covers the same steps.

## Recently completed

- 2026-11-06: The Windows release zip gets a `.zip.sha256` beside it, in
  `sha256sum` format, like the AppImage, `.deb` and `.rpm`. The build and
  release workflows upload it. Only CI has run the script.
- 2026-11-06: Released v1.9.5. PRs #13, #14 and #15 were merged into `main` in
  order and `v1.9.5` was tagged; the release workflow published all assets.
  The Linux loop tests passed as root on 2026-11-05, and a restore on a
  disposable USB drive succeeded. The 1.9.5 changes are in the changelog.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Keep completion notes
here only while a task is still in flight.

Design boundaries and intentional exclusions are in [polkit-helper.md](polkit-helper.md)
and [SECURITY.md](../SECURITY.md).

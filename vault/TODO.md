---
tags:
  - todo
---

# TODO

Working list for USB Restoration Tool. Check a box when it is done, then move the line to **Done** with a short note (date, what was verified).

## Now

These are the gaps that currently matter. The remaining code-review item is the one that blocks 1.4.0.

- [ ] **Pre-check filesystem geometry on Linux before erasing anything.** *(code review, 2026-08-16)* `canFormatFileSystem()` at `src/linux/linux_disk_service.cpp:74` returns `true` from every enum case, which makes the `if (reason) { … } return false;` tail unreachable. FAT32 on a 4 TB USB HDD therefore clears the signatures, writes the table and publishes the partition, and only then does `mkfs.vfat -F 32` refuse the volume, leaving the drive blank; an undersized stick fails the same way on the minimum-cluster side. Windows already refuses both combinations up front (`src/win/windows_disk_service.cpp:86`). Mirror that, for the same reason `resolveFormatTool` was hoisted ahead of the destructive work.
- [ ] **Linux hardware: restore a real USB stick.** No restore has ever run against physical media on Linux. Use a drive you can afford to lose. Cover at least: an ISO-written stick, a multi-partition stick, and a stick with no partition table. Both GPT and MBR. Log results in [[Hardware Testing]].
- [ ] **Exercise the polkit helper path.** `pkexec` → `usb-restoration-helper` has never run, even once. Needs a machine with polkit, an installed build (not the AppImage), and a disposable stick. Confirm the desktop password prompt, progress lines, cancel-while-reversible, and a finished exFAT volume.
- [ ] **Exercise the Linux AppImage path.** Still runs the whole app as root. Same hardware matrix as the helper path; they are different code routes.
- [ ] **Record which Windows restores were already verified.** The README says the Windows path has been verified on hardware, but not which models, sizes, or layouts. Capture that in [[Hardware Testing]] so it does not have to be rediscovered.

## Next

- [ ] **Distribution packages** (`.deb`, `.rpm`, AUR). Privilege separation only works when the helper and the polkit policy are installed to system paths. The AppImage cannot do that. Packaging is why this is 1.3.0 and not 1.2.0. See [[Linux]] and [[Releases]].
- [ ] **Windows Authenticode signing** for releases, if a certificate is available. Unsigned builds work; they show "Unknown" publisher and may trip SmartScreen. See `docs/signing.md` and [[Releases]].
- [ ] **Linux sector-size matrix.** Unit tests and `verify-partition-tables.sh` cover 512 and 4096. Confirm a 4K-native stick on hardware if one is on hand.
- [ ] **Pick a usable FAT32 allocation unit, not merely a legal one.** *(code review, 2026-08-16)* The loop at `src/core/safety.cpp:334` returns the first, and therefore smallest, unit whose cluster count lands in range. A 1 GiB volume with 512-byte sectors gets 512-byte clusters: roughly 2.1 M clusters and about 16 MB of FAT across both copies, where `format /fs:FAT32` would use 4 KiB. Either take the largest unit that still meets `minimumClusters`, or omit `AllocationUnitSize` from the WMI call when the Windows default is already valid. `tests/test_core.cpp:2074` pins the current answer and has to move with it.
- [ ] **Drop the dead NTFS retry.** *(code review, 2026-08-16)* `src/linux/linux_restore.cpp:167` follows `mkntfs -Q` with `mkntfs -f`, but `-Q/--quick` and `-f/--fast` are aliases for the same flag, so the unknown-option trigger can never reject one and accept the other. Harmless, but it reads like genuine second-dialect coverage the way the exfatprogs/exfat-utils fallback is.

## Later

- [ ] **macOS.** Out of scope until someone implements `DiskService` and nothing else. Do not teach `src/core` or the GUI about Darwin.
- [ ] **AppImage + polkit.** Fundamentally blocked: polkit reads policies from system directories, which a self-contained bundle cannot register. Do not spend design time on a workaround that reintroduces trusting the caller.
- [ ] **Windows privilege split.** Deliberately not done. UAC already gates the process at launch; a service or COM elevation moniker would add more attack surface than it removes. See [[Decisions]].

## Done

- [x] **ext4 restores leave a volume the user cannot write to** — 2026-08-18. `mkfs.ext4` now takes `-E root_owner=<uid>:<gid>`, resolved from `PKEXEC_UID` (pkexec helper path) or `SUDO_UID` (sudo and AppImage paths), with the group from the account's `passwd` entry and `SUDO_GID` only as a fallback. A root login sets neither and is left at `root:root`, which is what mkfs would write anyway; both branches say which they took through `reporter.detail()`. Verified that `mkfs.ext4 -F -L … -m 0 -E root_owner=1000:1000` in exactly that argument order produces a root inode owned by `1000:1000` (`debugfs -R 'stat <2>'` on a sparse image). Linux Release build and the unit tests pass. The environment lookup itself is not verified on hardware — it needs the helper and AppImage runs still listed above.
- [x] **Make the clangd database platform-correct** — 2026-08-17. Linux configure now symlinks the generator's `compile_commands.json` instead of shadowing it, writes `src/win/.clangd` (`Diagnostics.Suppress: ['*']`) so `Windows.h` is not diagnosed, drops GCC's `-mno-direct-extern-access` (unknown to Clang), and does not fail on a read-only checkout. `clangd --check` on `linux_disk_service.cpp` and `raw_disk.h` reports 0 errors; `linux_restore.cpp` has no compile diagnostics. Windows still synthesises a clang-style database (Visual Studio never emits one).
- [x] **Block undersized FAT32 targets before erasing** — 2026-08-16. Windows now derives the partition from the disk's real sector size, selects an explicit valid FAT32 allocation unit, and refuses volumes outside the formatter's cluster and 32 GiB limits. Windows Release build and boundary tests pass; not verified on hardware.
- [x] **Update `SECURITY.md` supported versions** — 2026-08-16. Only the current 1.4.x release line is supported; 1.3.x and older releases are not.
- [x] **Clangd errors (`core/disk.h` / Qt headers not found)** — 2026-08-16. Visual Studio generator never emits `compile_commands.json`; configure now writes a clang-style one at the source root. Restart clangd after configure.
- [x] **Filesystem choice (exFAT, FAT32, NTFS, ext4 on Linux)** — 1.4.0, 2026-08-16. Unit tests pass. Partition-dump cases for FAT32 MBR and ext4 GPT added to `verify-partition-tables.sh`. Not verified on hardware.
- [x] Linux backend (sysfs, mountinfo, raw block I/O, `mkfs.exfat`) — 1.1.0, compile-tested only
- [x] GPT or MBR, same 1 MiB-aligned layout — 1.1.0
- [x] `DiskService` interface; GUI and safety rules platform-agnostic — 1.1.0
- [x] Partition tables checked against `sgdisk` and `fdisk` in CI — 1.1.0
- [x] Identity check on the open handle before the first write — 1.0.0
- [x] Acknowledgement checkbox replacing the typed confirmation phrase — 1.1.0
- [x] Linux privilege split: unprivileged GUI + `usb-restoration-helper` — 1.3.0, **unverified on hardware**
- [x] Helper re-derives every fact; arguments are claims it may only refuse on — 1.3.0
- [x] AppImage kept on the run-as-root path — 1.3.0
- [x] Windows restore path verified on hardware — date and devices not recorded here

## Rules for this list

- A safety-rule change is not done until `tests/test_core.cpp` covers the case it now refuses.
- A new partition layout is not done until `scripts/verify-partition-tables.sh` has a case for it.
- Do not add a GUI command-line flag that starts a restore.
- Do not widen the USB bus-type check.
- Do not add a bypass for the acknowledgement dialog or the identity check.

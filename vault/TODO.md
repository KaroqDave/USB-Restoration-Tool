---
tags:
  - todo
---

# TODO

Working list for USB Restoration Tool. Check a box when it is done, then move the line to **Done** with a short note (date, what was verified).

## Now

These are the gaps that currently matter.

- [ ] **Linux hardware: restore a real USB stick.** No restore has ever run against physical media on Linux. Use a drive you can afford to lose. Cover at least: an ISO-written stick, a multi-partition stick, and a stick with no partition table. Both GPT and MBR. Log results in [[Hardware Testing]].
- [ ] **Exercise the polkit helper path.** `pkexec` → `usb-restoration-helper` has never run, even once. Needs a machine with polkit, an installed build (not the AppImage), and a disposable stick. Confirm the desktop password prompt, progress lines, cancel-while-reversible, and a finished exFAT volume.
- [ ] **Exercise the Linux AppImage path.** Still runs the whole app as root. Same hardware matrix as the helper path; they are different code routes.
- [ ] **Record which Windows restores were already verified.** The README says the Windows path has been verified on hardware, but not which models, sizes, or layouts. Capture that in [[Hardware Testing]] so it does not have to be rediscovered.

## Next

- [ ] **Update `SECURITY.md` supported versions.** The table still lists 1.1.x as the supported line. Current release is 1.3.0; 1.3.x should be listed, and whether 1.1.x remains supported should be a conscious choice.
- [ ] **Distribution packages** (`.deb`, `.rpm`, AUR). Privilege separation only works when the helper and the polkit policy are installed to system paths. The AppImage cannot do that. Packaging is why this is 1.3.0 and not 1.2.0. See [[Linux]] and [[Releases]].
- [ ] **Windows Authenticode signing** for releases, if a certificate is available. Unsigned builds work; they show "Unknown" publisher and may trip SmartScreen. See `docs/signing.md` and [[Releases]].
- [ ] **Linux sector-size matrix.** Unit tests and `verify-partition-tables.sh` cover 512 and 4096. Confirm a 4K-native stick on hardware if one is on hand.

## Later

- [ ] **macOS.** Out of scope until someone implements `DiskService` and nothing else. Do not teach `src/core` or the GUI about Darwin.
- [ ] **AppImage + polkit.** Fundamentally blocked: polkit reads policies from system directories, which a self-contained bundle cannot register. Do not spend design time on a workaround that reintroduces trusting the caller.
- [ ] **Windows privilege split.** Deliberately not done. UAC already gates the process at launch; a service or COM elevation moniker would add more attack surface than it removes. See [[Decisions]].

## Done

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

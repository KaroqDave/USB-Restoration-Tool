# Tasks

## Next

- Re-test the Windows restore after the review fixes in PR #7: one ordinary
  stick, which now also gets the empty GPT and the letterless-volume pass, and
  if possible a superfloppy again. Only the earlier version, which wrote the
  GPT when it found the placeholder, has been run on hardware.

## Recently completed

- 2026-10-21: Windows restores a "superfloppy" stick (bootloader on the bare
  disk, no partition table), which failed with `DeleteObject` return value 1
  and then error 21 on the first write. Windows fakes a whole-disk partition
  with an offline volume that blocks `WriteFile`. Every restore now writes an
  empty GPT through the raw handle after the WMI deletes and waits for the old
  volumes to leave the disk. A RAW reset was not enough, since it keeps the
  boot code's jump instruction. The earlier version restored an affected stick
  to GPT exFAT; Release build and core tests pass.

- 2026-10-21: Hardened the helper's trust boundary. Argument sizes accept plain
  digits only, a repeated version detail or second announcement is refused, and
  protocol text stays within its limit without splitting a surrogate pair.
  `usb-core-fuzz` fuzzes the parsers and partition tables and replays
  `tests/fuzz-corpus` in ctest; the `sanitizers` CI job runs the core tests
  and partition-table checks under ASan and UBSan and fuzzes for two minutes.
  CI passes on all four jobs. Ships in 1.6.2.

- 2026-10-18: Step plans for both backends moved to `src/core/restore_steps.*`
  with tests pinning the step count and first destructive step. The helper
  announces its plan and app version (protocol 3), and the GUI refuses a
  mismatched helper. CI passes on Windows and Linux, and a restore with the
  installed package is tested and confirmed working. Ships in 1.6.1.

- 2026-10-18: Added a `.deb` built by CPack through `scripts/build-deb.sh`, with
  the helper at `/usr/libexec/usb-restoration-helper` and its polkit action. CI
  builds, checks and installs it; releases attach it. Installing it and restoring
  through pkexec is tested and confirmed working. No other package formats are
  planned; the `.deb` and AppImage cover Linux.

- 2026-10-18: The Linux helper catches `SIGPIPE`/`SIGHUP`, and both backends
  confirm the disk again after the partition table is written and before
  formatting. SECURITY.md updated to match. Tested and confirmed working on both
  platforms.

- 2026-10-18: Clarified cancellation feedback about remounting and drive letters.
  Windows Release build and core tests passed.
- 2026-10-18: Consolidated documentation into `docs/`, removed the Obsidian vault,
  hardware-test log and signing guide, and shortened the README. Retired hardware
  coverage tasks following confirmation of testing across devices and systems.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Design boundaries and
intentional exclusions are in [polkit-helper.md](polkit-helper.md) and
[SECURITY.md](../SECURITY.md).

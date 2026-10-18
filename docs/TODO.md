# Tasks

## Next

- [ ] Test that each backend's reported step count and first destructive step
  match the restore sequence. Also handle GUI/helper version skew explicitly;
  a local step-count test alone cannot detect an older installed helper.
- [ ] Add Linux distribution packaging, starting with one of `.deb`, `.rpm` or AUR.
  Install the helper and polkit policy together at their configured paths.

## Recently completed

- 2026-10-18: Clarified cancellation feedback about remounting and drive letters.
  Windows Release build and core tests passed.
- 2026-10-18: Consolidated documentation into `docs/`, removed the Obsidian vault,
  hardware-test log and signing guide, and shortened the README. Retired hardware
  coverage tasks following confirmation of testing across devices and systems.

Release history is in [CHANGELOG.md](../CHANGELOG.md). Design boundaries and
intentional exclusions are in [polkit-helper.md](polkit-helper.md) and
[SECURITY.md](../SECURITY.md).

---
tags:
  - release
---

# Releases

`PROJECT_VERSION` in `CMakeLists.txt` is the single source of the version. Current: **1.4.2**.

Tagging `vX.Y.Z` triggers `.github/workflows/release.yml`. The workflow fails unless:

1. The tag matches `PROJECT_VERSION`
2. `README.md` has a `## What's New in X.Y.Z` section (used as the release body; `### Previously, in X.Y.Z` is also accepted after a later version has demoted it). The version is matched as a prefix, so the heading may carry a trailing note — `## What's New in 1.4.2 (unreleased)` is the section for 1.4.2. The suffix has to start with a space, so 1.4.2 does not match the heading for 1.4.20.

There is no 1.2.0. Privilege separation was always planned as 1.3.0 because it depends on a real install, which is a packaging project of its own.

## Line

| Version | Date | What |
|---|---|---|
| 1.4.2 | unreleased | Filesystem choice: exFAT, FAT32, NTFS, ext4 on Linux; Linux formatter fixes (ext4 ownership, FAT32 geometry) |
| 1.3.0 | 2026-08-06 | Linux privilege split (helper + polkit). Unverified on hardware. |
| 1.1.0 | 2026-08-06 | Linux backend, GPT/MBR, activity window, acknowledgement checkbox |
| 1.0.0 | | Identity-on-handle, WMI 16-bit property fix, job-queued-as-4096 fix, rebuilt UI |

Changelog: repo `CHANGELOG.md`.

## Packaging

- **Windows** — `scripts/build-standalone.ps1` → `standalone\USB-Restoration-Tool` + `SHA256SUMS.txt` + zip. Optional `-Sign -CertificateThumbprint` / `-RequireSignature`. See `docs/signing.md`.
- **Linux AppImage** — `scripts/build-appimage.sh`. Helper off. Runs as root.
- **Linux install** — helper + polkit policy. This is the privilege-separated path. Distro packages (`.deb` / `.rpm` / AUR) are still on [[TODO]].

## Related

- [[Linux]]
- [[TODO]]
- [[Development]]

# Changelog

## 0.1.0 - 2026-05-25

- Initial Windows-native Qt 6 release.
- Restores selected USB disks to one GPT + exFAT volume labeled `USB`.
- Uses direct Windows disk APIs for raw disk cleanup and GPT layout creation.
- Uses WMI Storage APIs for disk metadata refresh, partition deletion, and exFAT formatting.
- Adds safety checks for boot/system disks, `C:`, stale target identity, read-only/offline media, and large USB disks.
- Packages a portable Windows x64 ZIP with Qt/MSVC runtime dependencies and full-bundle SHA256 hashes.

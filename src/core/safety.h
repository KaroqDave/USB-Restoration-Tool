#pragma once

#include "core/disk.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace usbrestore {

struct GptLayout {
    std::uint64_t startOffset = 0;
    std::uint64_t length = 0;
};

// Below this a device is more likely misreporting its geometry than a real stick.
inline constexpr std::uint64_t MinimumRestoreTargetBytes = 8ull * 1024ull * 1024ull;

// A disk this large is more often an external SSD of backups than a boot stick.
inline constexpr std::uint64_t LargeRestoreTargetBytes = 128ull * 1024ull * 1024ull * 1024ull;

// Supplied by the platform backend, the only place that knows what must not be touched.
struct RestoreGuard {
    // "C:", "/", "/boot", "/home", ...
    QStringList mountPoints;
    // "\\.\PhysicalDrive0", "/dev/nvme0n1", ...
    QStringList deviceIds;
};

// A POSIX path also matches when it contains a protected path, so a disk
// mounted at "/" is refused for holding "/boot".
bool isProtectedMountPoint(const QString &mountPoint, const QStringList &protectedMountPoints);

bool containsProtectedMountPoint(const DiskInfo &disk, const RestoreGuard &guard);

// Every check is a refusal, not a warning.
bool isSafeRestoreTarget(const DiskInfo &disk, const RestoreGuard &guard, QString *reason = nullptr);

// An identifier only one side reports is skipped rather than counted as a match.
bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason = nullptr);

// The product name may contain underscores, so a candidate that is part of it is
// dropped rather than passed off as a serial that isSameRestoreTarget() trusts.
QString serialFromUsbByIdLink(const QString &link, const QString &productName);

bool isLargeRestoreTarget(const DiskInfo &disk);
QString largeRestoreTargetWarning(const DiskInfo &disk);

// Shared by the layout calculator and the table writer so the reserved space
// cannot drift from what is written and overlap a header.
inline constexpr quint32 GptEntryCount = 128;
inline constexpr quint32 GptEntrySize = 128;

quint32 gptEntryArraySectors(quint32 sectorSize);

// MBR reserves the same space: it costs a megabyte and keeps the partition
// aligned to every erase block size. A zero length means the disk is too small.
GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize);

// Zero means no supported geometry produces FAT32's required cluster count.
quint32 fat32AllocationUnitSize(std::uint64_t volumeSize, quint32 sectorSize, std::uint64_t maximumVolumeSize);

// mkfs.vfat -F 32 exits successfully outside these limits and leaves a wrong
// volume, so they are enforced before erasing. Zero means an unsupported sector size.
std::uint64_t minimumMkfsFat32VolumeBytes(quint32 sectorSize);
std::uint64_t maximumMkfsFat32VolumeBytes(quint32 sectorSize);

bool isSupportedSectorSize(quint32 sectorSize);

// Shared by both backends' writeZeros(): rounding for the caller would silently
// leave the tail unzeroed. Empty means the range is writable as-is.
QString unalignedZeroRangeError(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize);

} // namespace usbrestore

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

// Smallest disk worth restoring. Anything below this cannot hold a partition
// table, a 1 MiB-aligned partition, and a backup header with room left over,
// and a device reporting less than this is far more likely to be misreporting
// its geometry than to be a real 4 MiB USB stick.
inline constexpr std::uint64_t MinimumRestoreTargetBytes = 8ull * 1024ull * 1024ull;

// Above this, an extra confirmation is required: a 256 GB "USB disk" is much
// more often an external SSD full of backups than a boot stick to be wiped.
inline constexpr std::uint64_t LargeRestoreTargetBytes = 128ull * 1024ull * 1024ull * 1024ull;

// The places a restore must never touch, supplied by the platform backend
// because only it knows what they are: on Windows the drive holding Windows
// and the drive the app runs from, on Linux the filesystems the running system
// is built out of and the disk they live on.
struct RestoreGuard {
    // "C:", "/", "/boot", "/home", ...
    QStringList mountPoints;
    // "\\\\.\\PhysicalDrive0", "/dev/nvme0n1", ...
    QStringList deviceIds;
};

// Whether a mount point belongs to the protected set. A Windows drive letter
// matches exactly; a POSIX path also matches when it *contains* a protected
// path, so a disk mounted at "/" is refused for holding "/boot".
bool isProtectedMountPoint(const QString &mountPoint, const QStringList &protectedMountPoints);

bool containsProtectedMountPoint(const DiskInfo &disk, const RestoreGuard &guard);

// Whether the disk may be restored at all. Every check here is a refusal, not
// a warning: a disk that fails one is never offered to the restore path.
bool isSafeRestoreTarget(const DiskInfo &disk, const RestoreGuard &guard, QString *reason = nullptr);

// Whether a freshly enumerated disk is still the disk the user selected. The
// device id must match; every other identifier both sides report must agree,
// and one that only one side reports is skipped rather than counted as a
// match.
bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason = nullptr);

// udev names a USB disk "usb-<vendor>_<product>_<serial>-<host>:<lun>". The
// product itself may contain underscores, so counting fields is not enough:
// "usb-Generic_Flash_Disk-0:0" has three parts whose tail is the last word of
// the product. A candidate that is a substring of the product name is dropped
// rather than presented as a serial that isSameRestoreTarget() would treat
// as identity. Empty means "no serial", which is what the helper already
// does when the GUI has nothing to send.
QString serialFromUsbByIdLink(const QString &link, const QString &productName);

bool isLargeRestoreTarget(const DiskInfo &disk);
QString largeRestoreTargetWarning(const DiskInfo &disk);

// A GPT entry array is 128 entries of 128 bytes each. The constants live here
// rather than in the table writer because the layout calculator has to reserve
// exactly what the writer will go on to occupy: two places deriving the same
// number separately is how a layout comes to pass the write gate and then
// overlap a header.
inline constexpr quint32 GptEntryCount = 128;
inline constexpr quint32 GptEntrySize = 128;

// Sectors taken by one copy of the entry array, rounded up to whole sectors:
// 32 at 512 bytes per sector, 4 at 4096. One GPT copy is this plus its own
// header sector.
quint32 gptEntryArraySectors(quint32 sectorSize);

// The single 1 MiB-aligned data partition a restore creates, leaving room for
// the partition table at the start and, under GPT, the backup header at the
// end. A zero length means the disk is too small to lay out safely. The
// reservation is the same for MBR: it costs a megabyte and keeps the partition
// aligned to every erase block size in use.
GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize);

// Chooses the smallest allocation unit that makes a volume valid FAT32 within
// the formatter's size limit. A zero return means no supported geometry can
// produce FAT32's required data-cluster count.
quint32 fat32AllocationUnitSize(std::uint64_t volumeSize, quint32 sectorSize, std::uint64_t maximumVolumeSize);

// The FAT32 volume sizes mkfs.vfat can be trusted with, which are not the ones
// the Windows formatter enforces and are not a single number: both ends move
// with the sector size. dosfstools refuses neither edge on its own. Given an
// explicit -F 32 it waives its own minimum-cluster rule and only warns
// (mkfs.fat.c:898), and it clamps a sector count that overflows the 32-bit
// field it stores rather than failing (mkfs.fat.c:780). Both exit successfully
// and leave a wrong volume behind — an out-of-spec filesystem that strict FAT
// drivers may read as FAT16, or one that covers only the first part of the
// disk — so the refusal has to happen before anything is erased.
//
// Zero from either means the sector size is not one this tool writes.
std::uint64_t minimumMkfsFat32VolumeBytes(quint32 sectorSize);
std::uint64_t maximumMkfsFat32VolumeBytes(quint32 sectorSize);

// Whether a reported sector size can be trusted for raw writes. Anything else
// would make every offset this tool computes wrong.
bool isSupportedSectorSize(quint32 sectorSize);

} // namespace usbrestore

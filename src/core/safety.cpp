#include "core/safety.h"

#include <QRegularExpression>
#include <QtGlobal>

#include <array>
#include <limits>

namespace usbrestore {

namespace {

QString normalizedIdentity(const QString &value)
{
    return value.trimmed().toCaseFolded();
}

// The bare "C" form matters: the Windows guard holds single letters while disks
// report "E:", and the two would otherwise never match.
bool looksLikeDriveLetter(const QString &value)
{
    if (value.size() == 1) {
        return value.at(0).isLetter();
    }
    return value.size() >= 2 && value.at(1) == QLatin1Char(':');
}

QString normalizedMountPoint(const QString &value)
{
    QString mount = value.trimmed();
    if (mount.isEmpty()) {
        return {};
    }

    if (looksLikeDriveLetter(mount)) {
        return mount.left(1).toUpper();
    }

    mount.replace(QLatin1Char('\\'), QLatin1Char('/'));
    while (mount.size() > 1 && mount.endsWith(QLatin1Char('/'))) {
        mount.chop(1);
    }
    return mount;
}

bool bothKnownAndDifferent(const QString &left, const QString &right)
{
    const QString a = normalizedIdentity(left);
    const QString b = normalizedIdentity(right);
    return !a.isEmpty() && !b.isEmpty() && a != b;
}

// Not the negation of the above: an empty protected entry must match nothing.
bool bothKnownAndEqual(const QString &left, const QString &right)
{
    const QString a = normalizedIdentity(left);
    const QString b = normalizedIdentity(right);
    return !a.isEmpty() && !b.isEmpty() && a == b;
}

std::uint64_t fat32DataClusterCount(std::uint64_t volumeSize, quint32 sectorSize, quint32 allocationUnitSize)
{
    constexpr std::uint64_t reservedSectors = 32;
    constexpr std::uint64_t fatCount = 2;
    constexpr std::uint64_t fatEntryBytes = 4;

    if (sectorSize == 0 || allocationUnitSize < sectorSize || allocationUnitSize % sectorSize != 0 ||
        volumeSize % sectorSize != 0) {
        return 0;
    }

    const std::uint64_t totalSectors = volumeSize / sectorSize;
    if (totalSectors <= reservedSectors) {
        return 0;
    }

    const std::uint64_t sectorsPerCluster = allocationUnitSize / sectorSize;
    std::uint64_t low = 0;
    std::uint64_t high = (totalSectors - reservedSectors) / sectorsPerCluster;

    // Counts the FATs and reserved area, which volumeSize / allocationUnitSize
    // ignores and which can move a volume across the FAT16/FAT32 boundary.
    while (low < high) {
        const std::uint64_t clusters = low + (high - low + 1) / 2;
        const std::uint64_t fatBytes = (clusters + 2) * fatEntryBytes;
        const std::uint64_t fatSectors = (fatBytes + sectorSize - 1) / sectorSize;
        const std::uint64_t requiredSectors = reservedSectors + fatCount * fatSectors + clusters * sectorsPerCluster;
        if (requiredSectors <= totalSectors) {
            low = clusters;
        } else {
            high = clusters - 1;
        }
    }

    return low;
}

} // namespace

bool isProtectedMountPoint(const QString &mountPoint, const QStringList &protectedMountPoints)
{
    const QString candidate = normalizedMountPoint(mountPoint);
    if (candidate.isEmpty()) {
        return false;
    }

    for (const QString &entry : protectedMountPoints) {
        const QString guarded = normalizedMountPoint(entry);
        if (guarded.isEmpty()) {
            continue;
        }
        if (candidate == guarded) {
            return true;
        }

        // A disk mounted at "/" holds "/boot" whether or not that is a separate mount.
        if (!looksLikeDriveLetter(candidate) && !looksLikeDriveLetter(guarded)) {
            const QString prefix = candidate == QStringLiteral("/") ? candidate : candidate + QLatin1Char('/');
            if (guarded.startsWith(prefix)) {
                return true;
            }
        }
    }
    return false;
}

bool containsProtectedMountPoint(const DiskInfo &disk, const RestoreGuard &guard)
{
    for (const QString &mountPoint : disk.mountPoints) {
        if (isProtectedMountPoint(mountPoint, guard.mountPoints)) {
            return true;
        }
    }
    return false;
}

bool isSupportedSectorSize(quint32 sectorSize)
{
    switch (sectorSize) {
    case 512:
    case 1024:
    case 2048:
    case 4096:
        return true;
    default:
        return false;
    }
}

QString unalignedZeroRangeError(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize)
{
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    if (offset % sector == 0 && bytes % sector == 0) {
        return {};
    }
    return QStringLiteral("Refusing to zero %1 bytes at offset %2: not a multiple of the %3-byte sector size.")
        .arg(bytes)
        .arg(offset)
        .arg(sector);
}

bool isPartitionAtDiskStart(std::optional<std::uint64_t> partitionOffset)
{
    return partitionOffset.has_value() && *partitionOffset == 0;
}

std::uint32_t boundedEntryCount(
    std::uint32_t reportedCount, std::uint64_t returnedBytes, std::uint64_t headerBytes, std::uint64_t entryBytes)
{
    if (entryBytes == 0 || returnedBytes < headerBytes) {
        return 0;
    }
    const std::uint64_t fits = (returnedBytes - headerBytes) / entryBytes;
    return static_cast<std::uint32_t>(qMin<std::uint64_t>(reportedCount, fits));
}

bool isSafeRestoreTarget(const DiskInfo &disk, const RestoreGuard &guard, QString *reason)
{
    const auto refuse = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };

    const QString name = disk.displayId.isEmpty() ? disk.deviceId : disk.displayId;

    if (disk.deviceId.trimmed().isEmpty()) {
        return refuse(QStringLiteral("This disk reports no device path to restore."));
    }

    // No "unknown is fine" escape: an unreadable bus is a reason to stop.
    if (disk.busType != UsbBusType) {
        return refuse(QStringLiteral("%1 is on the %2 bus, not USB.").arg(name, busTypeName(disk.busType)));
    }

    for (const QString &protectedDevice : guard.deviceIds) {
        if (bothKnownAndEqual(disk.deviceId, protectedDevice)) {
            return refuse(QStringLiteral("%1 is a disk the running system depends on.").arg(name));
        }
    }

    if (disk.isBoot || disk.isSystem) {
        return refuse(QStringLiteral("%1 is marked as a boot or system disk.").arg(name));
    }

    if (containsProtectedMountPoint(disk, guard)) {
        return refuse(QStringLiteral("%1 holds a protected system location.").arg(name));
    }

    if (disk.isOffline) {
        return refuse(QStringLiteral("%1 is offline.").arg(name));
    }

    if (disk.isReadOnly) {
        return refuse(QStringLiteral("%1 is read-only.").arg(name));
    }

    if (disk.size == 0) {
        return refuse(QStringLiteral("%1 reports no usable media.").arg(name));
    }

    if (disk.size < MinimumRestoreTargetBytes) {
        return refuse(
            QStringLiteral("%1 reports only %2, which is too small to restore.").arg(name, formatByteSize(disk.size)));
    }

    if (!isSupportedSectorSize(disk.sectorSize)) {
        return refuse(
            QStringLiteral("%1 reports an unsupported sector size of %2 bytes.").arg(name).arg(disk.sectorSize));
    }

    if (calculateGptLayout(disk.size, disk.sectorSize).length == 0) {
        return refuse(QStringLiteral("%1 is too small for a safe partition layout.").arg(name));
    }

    return true;
}

bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason)
{
    const QString name = selected.displayId.isEmpty() ? selected.deviceId : selected.displayId;

    const auto refuse = [reason, &name](const QString &what) {
        if (reason) {
            *reason =
                QStringLiteral("%1 no longer reports the same %2. Refresh and select the USB again.").arg(name, what);
        }
        return false;
    };

    if (normalizedIdentity(selected.deviceId) != normalizedIdentity(current.deviceId)) {
        if (reason) {
            *reason = QStringLiteral("The selected disk moved from %1 to %2. Refresh and select the USB again.")
                          .arg(selected.deviceId, current.deviceId);
        }
        return false;
    }

    if (selected.size != current.size) {
        return refuse(QStringLiteral("size"));
    }

    if (selected.deviceSequence && !isSameDeviceSequence(name, selected.deviceSequence, current.deviceSequence)) {
        return refuse(QStringLiteral("device instance"));
    }

    if (selected.sectorSize != 0 && current.sectorSize != 0 && selected.sectorSize != current.sectorSize) {
        return refuse(QStringLiteral("sector size"));
    }

    // Check all of them: a device can keep its serial while changing its path.
    if (bothKnownAndDifferent(selected.serialNumber, current.serialNumber)) {
        return refuse(QStringLiteral("serial number"));
    }
    if (bothKnownAndDifferent(selected.uniqueId, current.uniqueId)) {
        return refuse(QStringLiteral("unique ID"));
    }
    if (bothKnownAndDifferent(selected.path, current.path)) {
        return refuse(QStringLiteral("device path"));
    }
    if (bothKnownAndDifferent(selected.name, current.name)) {
        return refuse(QStringLiteral("device name"));
    }

    return true;
}

bool isSameDeviceSequence(
    const QString &device, std::optional<std::uint64_t> expected, std::optional<std::uint64_t> current, QString *reason)
{
    if (expected && current && *expected != 0 && *expected == *current) {
        return true;
    }
    if (reason) {
        *reason = QStringLiteral(
                      "Could not confirm that %1 is still the disk that was selected. Refresh and select "
                      "the USB again.")
                      .arg(device);
    }
    return false;
}

QString partitionLayoutError(PartitionStyle style, const GptLayout &layout, quint32 sectorSize)
{
    if (!isSupportedSectorSize(sectorSize)) {
        return QStringLiteral("Unsupported sector size of %1 bytes.").arg(sectorSize);
    }
    if (layout.length == 0) {
        return QStringLiteral("The disk is too small for a partition layout.");
    }
    if (style != PartitionStyle::Mbr && style != PartitionStyle::Gpt) {
        return QStringLiteral("No partition style was chosen.");
    }
    // 32-bit sector addresses: 2 TiB at 512 bytes, which an external drive can exceed.
    if (style == PartitionStyle::Mbr && (layout.startOffset + layout.length) / sectorSize > 0xFFFFFFFFull) {
        return QStringLiteral("MBR cannot address a partition this far into the disk. Choose GPT.");
    }
    return {};
}

bool canCreatePartitionLayout(PartitionStyle style, const DiskInfo &disk, QString *reason)
{
    const QString failure =
        partitionLayoutError(style, calculateGptLayout(disk.size, disk.sectorSize), disk.sectorSize);
    if (!failure.isEmpty() && reason) {
        *reason = failure;
    }
    return failure.isEmpty();
}

bool isValidDeviceRange(std::uint64_t size, std::uint64_t offset, std::uint64_t length, quint32 sectorSize)
{
    return isSupportedSectorSize(sectorSize) && length > 0 && offset <= size && length <= size - offset &&
           offset % sectorSize == 0 && length % sectorSize == 0;
}

QString serialFromUsbByIdLink(const QString &link, const QString &productName)
{
    if (!link.startsWith(QStringLiteral("usb-"))) {
        return {};
    }

    QString value = link;
    static const QRegularExpression hostSuffix(QStringLiteral("-[0-9]+:[0-9]+$"));
    value.remove(hostSuffix);

    const QStringList parts = value.mid(4).split(QLatin1Char('_'), Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return {};
    }

    const QString candidate = parts.last();
    // "Generic_Flash_Disk" passes the three-part rule; its tail is the product.
    if (!productName.isEmpty() && productName.contains(candidate, Qt::CaseInsensitive)) {
        return {};
    }
    return candidate;
}

bool isLargeRestoreTarget(const DiskInfo &disk)
{
    return disk.size >= LargeRestoreTargetBytes;
}

QString largeRestoreTargetWarning(const DiskInfo &disk)
{
    if (!isLargeRestoreTarget(disk)) {
        return {};
    }

    return QStringLiteral("Large USB disk: %1. Make sure this is not an external SSD or HDD with data you need.")
        .arg(formatByteSize(disk.size));
}

quint32 gptEntryArraySectors(quint32 sectorSize)
{
    const quint32 sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    const quint32 bytes = GptEntryCount * GptEntrySize;
    return (bytes + sector - 1) / sector;
}

GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize)
{
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    const std::uint64_t oneMiB = 1024ull * 1024ull;
    const std::uint64_t start = ((oneMiB + sector - 1) / sector) * sector;
    // Derived, not hard-coded, so it agrees with the write gate. Reserved under MBR
    // too, so the partition style never moves where the data starts or ends.
    const std::uint64_t backupGpt = (gptEntryArraySectors(sectorSize) + 1ull) * sector;

    if (diskSize <= start + backupGpt + sector) {
        return {};
    }

    // End on a 1 MiB boundary as fdisk, gdisk and Windows do; `sgdisk -v`
    // complains about a partition that ends anywhere else.
    const std::uint64_t alignment = qMax<std::uint64_t>(oneMiB, sector);
    const std::uint64_t rawLength = diskSize - start - backupGpt;
    const std::uint64_t length = (rawLength / alignment) * alignment;
    if (length == 0) {
        return {};
    }
    return {start, length};
}

quint32 fat32AllocationUnitSize(std::uint64_t volumeSize, quint32 sectorSize, std::uint64_t maximumVolumeSize)
{
    // The Windows format stack's limits; they also keep clear of FAT16's range.
    constexpr std::uint64_t minimumClusters = 65527;
    constexpr std::uint64_t maximumClusters = 4177917;
    constexpr std::array<quint32, 10> allocationUnitSizes = {
        512,
        1024,
        2048,
        4096,
        8192,
        16 * 1024,
        32 * 1024,
        64 * 1024,
        128 * 1024,
        256 * 1024,
    };

    if (!isSupportedSectorSize(sectorSize) || volumeSize == 0 || volumeSize > maximumVolumeSize) {
        return 0;
    }

    // format.com's default unit. The smallest legal unit is valid but degenerate:
    // 512-byte clusters give a 1 GiB volume about 16 MB of FAT.
    constexpr std::uint64_t oneMiB = 1024ull * 1024ull;
    constexpr std::uint64_t oneGiB = 1024ull * oneMiB;
    quint32 preferred = 32 * 1024;
    if (volumeSize <= 64 * oneMiB) {
        preferred = 512;
    } else if (volumeSize <= 128 * oneMiB) {
        preferred = 1024;
    } else if (volumeSize <= 256 * oneMiB) {
        preferred = 2048;
    } else if (volumeSize <= 8 * oneGiB) {
        preferred = 4096;
    } else if (volumeSize <= 16 * oneGiB) {
        preferred = 8 * 1024;
    } else if (volumeSize <= 32 * oneGiB) {
        preferred = 16 * 1024;
    }

    // Fall back to any legal unit: odd clusters beat a refusal.
    quint32 best = 0;
    for (const quint32 allocationUnitSize : allocationUnitSizes) {
        // Windows allows 128 and 256 KiB units only on sectors above 512 bytes.
        if ((allocationUnitSize > 64 * 1024 && sectorSize == 512) || allocationUnitSize < sectorSize ||
            allocationUnitSize % sectorSize != 0) {
            continue;
        }

        const std::uint64_t clusterCount = fat32DataClusterCount(volumeSize, sectorSize, allocationUnitSize);
        if (clusterCount < minimumClusters || clusterCount > maximumClusters) {
            continue;
        }
        if (best == 0 || allocationUnitSize <= preferred) {
            best = allocationUnitSize;
        }
    }

    return best;
}

std::uint64_t minimumMkfsFat32VolumeBytes(quint32 sectorSize)
{
    if (!isSupportedSectorSize(sectorSize)) {
        return 0;
    }

    // MIN_CLUST_32 in mkfs.fat.c, two short of the 65,527 Windows demands.
    constexpr std::uint64_t minimumClusters = 65525;
    constexpr std::uint64_t reservedSectors = 32;
    constexpr std::uint64_t fatCount = 2;
    constexpr std::uint64_t fatEntryBytes = 4;

    // One sector per cluster reaches the cluster minimum on the smallest volume.
    const std::uint64_t fatBytes = (minimumClusters + 2) * fatEntryBytes;
    const std::uint64_t fatSectors = (fatBytes + sectorSize - 1) / sectorSize;
    const std::uint64_t bare = (reservedSectors + fatCount * fatSectors + minimumClusters) * sectorSize;

    // mkfs.vfat's own alignment raises the floor by a few sectors (eleven at 512
    // bytes); rounding up to a mebibyte covers that without reproducing it.
    constexpr std::uint64_t oneMiB = 1024ull * 1024ull;
    return ((bare + oneMiB - 1) / oneMiB) * oneMiB;
}

std::uint64_t maximumMkfsFat32VolumeBytes(quint32 sectorSize)
{
    if (!isSupportedSectorSize(sectorSize)) {
        return 0;
    }

    // num_sectors in mkfs.fat.c is a uint32_t.
    return static_cast<std::uint64_t>(std::numeric_limits<quint32>::max()) * sectorSize;
}

} // namespace usbrestore

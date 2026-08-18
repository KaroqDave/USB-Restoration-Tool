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

// "C:", "C:\" and a bare "C" all name a Windows drive. The bare form matters:
// the Windows backend builds its guard from single letters while a disk
// reports its mounts as "E:", and without this the two would never match.
bool looksLikeDriveLetter(const QString &value)
{
    if (value.size() == 1) {
        return value.at(0).isLetter();
    }
    return value.size() >= 2 && value.at(1) == QLatin1Char(':');
}

// "e:\", "E:", "e" all name the same drive; "/run/media/dave/USB/" and
// "/run/media/dave/USB" the same directory.
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

// Both values are known and they differ. An identifier that one side does not
// report says nothing about identity, so it must not count as a mismatch.
bool bothKnownAndDifferent(const QString &left, const QString &right)
{
    const QString a = normalizedIdentity(left);
    const QString b = normalizedIdentity(right);
    return !a.isEmpty() && !b.isEmpty() && a != b;
}

// Deliberately not the negation of the above: an empty entry in the protected
// device list must match nothing, rather than matching everything.
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

    // Find the greatest data-cluster count whose reserved area, two FATs and
    // data region all fit. This includes the two reserved FAT entries and
    // avoids accepting a boundary that raw volumeSize / allocationUnitSize
    // would put just on the wrong side of FAT16/FAT32.
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

        // A POSIX mount point that contains a protected path is protected too:
        // a disk mounted at "/" holds "/boot" whether or not "/boot" was listed
        // as a separate mount. Drive letters have no such containment.
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

    // Checked before anything else and without an "unknown is fine" escape
    // hatch: an unreadable bus is a reason to stop, not a reason to continue.
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

    if (selected.sectorSize != 0 && current.sectorSize != 0 && selected.sectorSize != current.sectorSize) {
        return refuse(QStringLiteral("sector size"));
    }

    // Every identifier both sides report has to agree. Stopping at the first
    // strong identifier that matched let a device keep its serial number while
    // changing its path and still pass as unchanged.
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

QString serialFromUsbByIdLink(const QString &link, const QString &productName)
{
    if (!link.startsWith(QStringLiteral("usb-"))) {
        return {};
    }

    QString value = link;
    static const QRegularExpression hostSuffix(QStringLiteral("-[0-9]+:[0-9]+$"));
    value.remove(hostSuffix);

    if (!value.startsWith(QStringLiteral("usb-"))) {
        return {};
    }

    // "usb-" is a prefix, not a field. Counting parts of the remainder is what
    // distinguishes vendor_product_serial from vendor_product.
    const QStringList parts = value.mid(4).split(QLatin1Char('_'), Qt::SkipEmptyParts);
    if (parts.size() < 3) {
        return {};
    }

    const QString candidate = parts.last();
    // Product names with underscores already satisfy the three-part rule
    // ("Generic_Flash_Disk"). A tail that appears in the product is the
    // product, not a serial.
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

GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize)
{
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    const std::uint64_t oneMiB = 1024ull * 1024ull;
    const std::uint64_t start = ((oneMiB + sector - 1) / sector) * sector;
    // The backup GPT is one header plus a 128-entry partition array, which is
    // 33 sectors at 512 bytes each. Reserved under MBR too, so switching the
    // partition style never changes where the data starts or ends.
    const std::uint64_t backupGpt = 33ull * sector;

    if (diskSize <= start + backupGpt + sector) {
        return {};
    }

    // The length is rounded down to the same 1 MiB the partition starts on, so
    // the end lands on a 1 MiB boundary too. That is what fdisk, gdisk and
    // Windows all produce, and `sgdisk -v` complains about a partition that
    // ends anywhere else. It costs under a megabyte of a disk.
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
    // These are the limits enforced by the Windows format stack. Staying
    // within them also keeps the volume away from FAT16's cluster range.
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

    for (const quint32 allocationUnitSize : allocationUnitSizes) {
        // Windows only permits 128 KiB and 256 KiB FAT allocation units when
        // the physical sector is larger than 512 bytes.
        if ((allocationUnitSize > 64 * 1024 && sectorSize == 512) || allocationUnitSize < sectorSize ||
            allocationUnitSize % sectorSize != 0) {
            continue;
        }

        const std::uint64_t clusterCount = fat32DataClusterCount(volumeSize, sectorSize, allocationUnitSize);
        if (clusterCount >= minimumClusters && clusterCount <= maximumClusters) {
            return allocationUnitSize;
        }
    }

    return 0;
}

std::uint64_t minimumMkfsFat32VolumeBytes(quint32 sectorSize)
{
    if (!isSupportedSectorSize(sectorSize)) {
        return 0;
    }

    // MIN_CLUST_32 in mkfs.fat.c. Two clusters short of the 65,527 the Windows
    // formatter demands, because the two came from different vendors; the
    // stricter number is not the one dosfstools checks against.
    constexpr std::uint64_t minimumClusters = 65525;
    constexpr std::uint64_t reservedSectors = 32;
    constexpr std::uint64_t fatCount = 2;
    constexpr std::uint64_t fatEntryBytes = 4;

    // One sector per cluster: the smallest cluster mkfs.vfat can pick, and so
    // the geometry that reaches the cluster minimum on the smallest volume.
    // Anything below this cannot reach it at any cluster size.
    const std::uint64_t fatBytes = (minimumClusters + 2) * fatEntryBytes;
    const std::uint64_t fatSectors = (fatBytes + sectorSize - 1) / sectorSize;
    const std::uint64_t bare = (reservedSectors + fatCount * fatSectors + minimumClusters) * sectorSize;

    // Rounded up to a whole mebibyte. mkfs.vfat aligns the reserved area, each
    // FAT and the sector count itself, which moves the real floor up by a
    // handful of sectors — measured at eleven with 512-byte sectors, and not a
    // calculation worth reproducing here to chase. A megabyte is the alignment
    // the partition already uses, and the floor is around 33 MiB either way.
    constexpr std::uint64_t oneMiB = 1024ull * 1024ull;
    return ((bare + oneMiB - 1) / oneMiB) * oneMiB;
}

std::uint64_t maximumMkfsFat32VolumeBytes(quint32 sectorSize)
{
    if (!isSupportedSectorSize(sectorSize)) {
        return 0;
    }

    // num_sectors in mkfs.fat.c is a uint32_t, so the largest volume it can
    // describe is that many sectors — 2 TiB at 512 bytes, 16 TiB at 4096.
    return static_cast<std::uint64_t>(std::numeric_limits<quint32>::max()) * sectorSize;
}

} // namespace usbrestore

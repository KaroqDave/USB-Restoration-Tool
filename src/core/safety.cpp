#include "core/safety.h"

#include <QtGlobal>

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
            *reason = QStringLiteral("%1 no longer reports the same %2. Refresh and select the USB again.")
                          .arg(name, what);
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

} // namespace usbrestore

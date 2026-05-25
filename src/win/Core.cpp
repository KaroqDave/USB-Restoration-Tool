#include "win/Core.h"

#include <QtGlobal>

namespace usbrestore {

namespace {

constexpr std::uint64_t LargeRestoreTargetBytes = 128ull * 1024ull * 1024ull * 1024ull;

QString normalizedIdentity(const QString &value)
{
    return value.trimmed().toCaseFolded();
}

bool bothKnownAndDifferent(const QString &left, const QString &right)
{
    const QString a = normalizedIdentity(left);
    const QString b = normalizedIdentity(right);
    return !a.isEmpty() && !b.isEmpty() && a != b;
}

}

QString formatByteSize(std::uint64_t bytes)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return QStringLiteral("%1 %2").arg(static_cast<qulonglong>(bytes)).arg(units[unit]);
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 2).arg(units[unit]);
}

QString confirmationPhrase(quint32 diskNumber)
{
    return QStringLiteral("RESTORE DISK %1").arg(diskNumber);
}

QString firstAvailableDriveLetter(quint32 logicalDrivesMask)
{
    for (int code = 'D'; code <= 'Z'; ++code) {
        const QChar letter(static_cast<ushort>(code));
        const int bit = code - 'A';
        if ((logicalDrivesMask & (1u << bit)) == 0) {
            return QString(letter) + QStringLiteral(":\\");
        }
    }
    return {};
}

bool containsProtectedDriveLetter(const DiskInfo &disk)
{
    for (const QString &letter : disk.driveLetters) {
        if (letter.compare(QStringLiteral("C:"), Qt::CaseInsensitive) == 0 ||
            letter.compare(QStringLiteral("C:\\"), Qt::CaseInsensitive) == 0) {
            return true;
        }
    }
    return false;
}

bool isSafeRestoreTarget(const DiskInfo &disk, QString *reason)
{
    if (disk.busType != 0 && disk.busType != 7) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 is no longer reported as a USB disk.").arg(disk.number);
        }
        return false;
    }

    if (disk.isBoot || disk.isSystem) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 is marked as a boot or system disk.").arg(disk.number);
        }
        return false;
    }

    if (containsProtectedDriveLetter(disk)) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 contains C: and will not be restored.").arg(disk.number);
        }
        return false;
    }

    if (disk.isOffline) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 is offline.").arg(disk.number);
        }
        return false;
    }

    if (disk.isReadOnly) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 is read-only.").arg(disk.number);
        }
        return false;
    }

    if (disk.size == 0) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 reports no usable media.").arg(disk.number);
        }
        return false;
    }

    return true;
}

bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason)
{
    if (selected.number != current.number) {
        if (reason) {
            *reason = QStringLiteral("The selected disk number changed from %1 to %2. Refresh and select the USB again.")
                          .arg(selected.number)
                          .arg(current.number);
        }
        return false;
    }

    if (selected.size != current.size) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 no longer has the same size. Refresh and select the USB again.").arg(selected.number);
        }
        return false;
    }

    if (selected.sectorSize != 0 && current.sectorSize != 0 && selected.sectorSize != current.sectorSize) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 no longer has the same sector size. Refresh and select the USB again.").arg(selected.number);
        }
        return false;
    }

    if (bothKnownAndDifferent(selected.serialNumber, current.serialNumber)) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 no longer reports the same serial number. Refresh and select the USB again.").arg(selected.number);
        }
        return false;
    }

    if (selected.serialNumber.trimmed().isEmpty() && current.serialNumber.trimmed().isEmpty()) {
        if (bothKnownAndDifferent(selected.path, current.path)) {
            if (reason) {
                *reason = QStringLiteral("Disk %1 no longer reports the same device path. Refresh and select the USB again.").arg(selected.number);
            }
            return false;
        }

        if (selected.path.trimmed().isEmpty() && current.path.trimmed().isEmpty() &&
            bothKnownAndDifferent(selected.uniqueId, current.uniqueId)) {
            if (reason) {
                *reason = QStringLiteral("Disk %1 no longer reports the same unique ID. Refresh and select the USB again.").arg(selected.number);
            }
            return false;
        }
    }

    if (selected.serialNumber.trimmed().isEmpty() &&
        selected.path.trimmed().isEmpty() &&
        selected.uniqueId.trimmed().isEmpty() &&
        bothKnownAndDifferent(selected.name, current.name)) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 no longer has the same device name. Refresh and select the USB again.").arg(selected.number);
        }
        return false;
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

    return QStringLiteral("Large USB disk: %1. Make sure this is not an external SSD/HDD with data you need.")
        .arg(formatByteSize(disk.size));
}

GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize)
{
    const std::uint64_t sector = sectorSize == 0 ? 512 : sectorSize;
    const std::uint64_t oneMiB = 1024ull * 1024ull;
    const std::uint64_t start = ((oneMiB + sector - 1) / sector) * sector;
    const std::uint64_t backupGpt = 33ull * sector;

    if (diskSize <= start + backupGpt + sector) {
        return {};
    }

    const std::uint64_t rawLength = diskSize - start - backupGpt;
    const std::uint64_t length = (rawLength / sector) * sector;
    return {start, length};
}

}

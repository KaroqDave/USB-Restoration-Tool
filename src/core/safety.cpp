#include "core/safety.h"

#include <QtGlobal>

namespace usbrestore {

namespace {

QString normalizedIdentity(const QString &value)
{
    return value.trimmed().toCaseFolded();
}

QString normalizedLetter(const QString &value)
{
    QString letter = value.trimmed();
    while (letter.endsWith(QLatin1Char('\\')) || letter.endsWith(QLatin1Char('/'))) {
        letter.chop(1);
    }
    if (letter.endsWith(QLatin1Char(':'))) {
        letter.chop(1);
    }
    return letter.toUpper();
}

// Both values are known and they differ. An identifier that one side does not
// report says nothing about identity, so it must not count as a mismatch.
bool bothKnownAndDifferent(const QString &left, const QString &right)
{
    const QString a = normalizedIdentity(left);
    const QString b = normalizedIdentity(right);
    return !a.isEmpty() && !b.isEmpty() && a != b;
}

} // namespace

QString confirmationPhrase(quint32 diskNumber)
{
    return QStringLiteral("RESTORE DISK %1").arg(diskNumber);
}

QString firstAvailableDriveLetter(quint32 logicalDrivesMask)
{
    for (int code = 'D'; code <= 'Z'; ++code) {
        const int bit = code - 'A';
        if ((logicalDrivesMask & (1u << bit)) == 0) {
            return QString(QChar(static_cast<ushort>(code))) + QStringLiteral(":\\");
        }
    }
    return {};
}

bool containsProtectedDriveLetter(const DiskInfo &disk, const QStringList &extraProtectedLetters)
{
    QStringList protectedLetters{QStringLiteral("C")};
    for (const QString &letter : extraProtectedLetters) {
        const QString normalized = normalizedLetter(letter);
        if (!normalized.isEmpty() && !protectedLetters.contains(normalized)) {
            protectedLetters.append(normalized);
        }
    }

    for (const QString &letter : disk.driveLetters) {
        if (protectedLetters.contains(normalizedLetter(letter))) {
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

bool isSafeRestoreTarget(const DiskInfo &disk, QString *reason, const QStringList &extraProtectedLetters)
{
    const auto refuse = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };

    // Checked first and without an "unknown is fine" escape hatch: an
    // unreadable bus type is a reason to stop, not a reason to continue.
    if (disk.busType != UsbBusType) {
        return refuse(QStringLiteral("Disk %1 is on the %2 bus, not USB.")
                          .arg(disk.number)
                          .arg(busTypeName(disk.busType)));
    }

    if (disk.isBoot || disk.isSystem) {
        return refuse(QStringLiteral("Disk %1 is marked as a boot or system disk.").arg(disk.number));
    }

    if (containsProtectedDriveLetter(disk, extraProtectedLetters)) {
        return refuse(QStringLiteral("Disk %1 holds a protected Windows drive letter.").arg(disk.number));
    }

    if (disk.isOffline) {
        return refuse(QStringLiteral("Disk %1 is offline.").arg(disk.number));
    }

    if (disk.isReadOnly) {
        return refuse(QStringLiteral("Disk %1 is read-only.").arg(disk.number));
    }

    if (disk.size == 0) {
        return refuse(QStringLiteral("Disk %1 reports no usable media.").arg(disk.number));
    }

    if (disk.size < MinimumRestoreTargetBytes) {
        return refuse(QStringLiteral("Disk %1 reports only %2, which is too small to restore.")
                          .arg(disk.number)
                          .arg(formatByteSize(disk.size)));
    }

    if (!isSupportedSectorSize(disk.sectorSize)) {
        return refuse(QStringLiteral("Disk %1 reports an unsupported sector size of %2 bytes.")
                          .arg(disk.number)
                          .arg(disk.sectorSize));
    }

    if (calculateGptLayout(disk.size, disk.sectorSize).length == 0) {
        return refuse(QStringLiteral("Disk %1 is too small for a safe GPT layout.").arg(disk.number));
    }

    return true;
}

bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason)
{
    const auto refuse = [reason](const QString &what, quint32 diskNumber) {
        if (reason) {
            *reason = QStringLiteral("Disk %1 no longer reports the same %2. Refresh and select the USB again.")
                          .arg(diskNumber)
                          .arg(what);
        }
        return false;
    };

    if (selected.number != current.number) {
        if (reason) {
            *reason = QStringLiteral("The selected disk number changed from %1 to %2. Refresh and select the USB again.")
                          .arg(selected.number)
                          .arg(current.number);
        }
        return false;
    }

    if (selected.size != current.size) {
        return refuse(QStringLiteral("size"), selected.number);
    }

    if (selected.sectorSize != 0 && current.sectorSize != 0 && selected.sectorSize != current.sectorSize) {
        return refuse(QStringLiteral("sector size"), selected.number);
    }

    // Every identifier both sides report has to agree. The earlier version
    // stopped at the first strong identifier it found, so a device that kept
    // its serial number but changed its path went unnoticed.
    if (bothKnownAndDifferent(selected.serialNumber, current.serialNumber)) {
        return refuse(QStringLiteral("serial number"), selected.number);
    }
    if (bothKnownAndDifferent(selected.uniqueId, current.uniqueId)) {
        return refuse(QStringLiteral("unique ID"), selected.number);
    }
    if (bothKnownAndDifferent(selected.path, current.path)) {
        return refuse(QStringLiteral("device path"), selected.number);
    }
    if (bothKnownAndDifferent(selected.name, current.name)) {
        return refuse(QStringLiteral("device name"), selected.number);
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
    // The backup GPT is one header plus a 128-entry partition array; 33 sectors
    // is the layout Windows itself writes at 512 bytes per sector.
    const std::uint64_t backupGpt = 33ull * sector;

    if (diskSize <= start + backupGpt + sector) {
        return {};
    }

    const std::uint64_t rawLength = diskSize - start - backupGpt;
    const std::uint64_t length = (rawLength / sector) * sector;
    if (length == 0) {
        return {};
    }
    return {start, length};
}

} // namespace usbrestore

#include "core/disk.h"

#include <QCoreApplication>
#include <QtGlobal>

namespace usbrestore {

QString formatByteSize(std::uint64_t bytes, TextMode mode)
{
    static const char *units[] = {"B", "KB", "MB", "GB", "TB", "PB"};
    double value = static_cast<double>(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0) {
        return QStringLiteral("%1 %2").arg(static_cast<qulonglong>(bytes)).arg(QLatin1String(units[unit]));
    }
    return QStringLiteral("%1 %2").arg(formatDecimal(value, 2, mode)).arg(QLatin1String(units[unit]));
}

QString partitionStyleName(quint16 style, TextMode mode)
{
    switch (static_cast<PartitionStyle>(style)) {
    case PartitionStyle::Mbr:
        return QStringLiteral("MBR");
    case PartitionStyle::Gpt:
        return QStringLiteral("GPT");
    case PartitionStyle::Unknown:
        break;
    }
    // RAW is what Windows shows for a disk whose partition table it cannot
    // read, which is the usual state of a drive an ISO writer has been over.
    return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "RAW / unknown"), mode);
}

QString healthStatusName(quint16 status, TextMode mode)
{
    switch (static_cast<HealthStatus>(status)) {
    case HealthStatus::Healthy:
        return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Healthy"), mode);
    case HealthStatus::Warning:
        return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Warning"), mode);
    case HealthStatus::Unhealthy:
        return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Unhealthy"), mode);
    }
    return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Unknown"), mode);
}

QString busTypeName(quint32 busType, TextMode mode)
{
    switch (busType) {
    case 1:
        return QStringLiteral("SCSI");
    case 2:
        return QStringLiteral("ATAPI");
    case 3:
        return QStringLiteral("ATA");
    case 4:
        return QStringLiteral("1394");
    case 5:
        return QStringLiteral("SSA");
    case 6:
        return QStringLiteral("Fibre Channel");
    case UsbBusType:
        return QStringLiteral("USB");
    case 8:
        return QStringLiteral("RAID");
    case 9:
        return QStringLiteral("iSCSI");
    case 10:
        return QStringLiteral("SAS");
    case 11:
        return QStringLiteral("SATA");
    case 12:
        return QStringLiteral("SD");
    case 13:
        return QStringLiteral("MMC");
    case 17:
        return QStringLiteral("NVMe");
    default:
        return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Unknown"), mode);
    }
}

QString partitionStyleLabel(PartitionStyle style, TextMode mode)
{
    switch (style) {
    case PartitionStyle::Gpt:
        return QStringLiteral("GPT");
    case PartitionStyle::Mbr:
        return QStringLiteral("MBR");
    case PartitionStyle::Unknown:
        break;
    }
    return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Unknown"), mode);
}

QString fileSystemTypeName(FileSystemType type)
{
    switch (type) {
    case FileSystemType::ExFat:
        return QStringLiteral("exFAT");
    case FileSystemType::Fat32:
        return QStringLiteral("FAT32");
    case FileSystemType::Ntfs:
        return QStringLiteral("NTFS");
    case FileSystemType::Ext4:
        return QStringLiteral("ext4");
    }
    return QStringLiteral("Unknown");
}

QString fileSystemTypeLabel(FileSystemType type)
{
    switch (type) {
    case FileSystemType::ExFat:
        return QCoreApplication::translate("usbrestore::Disk", "exFAT (recommended)");
    case FileSystemType::Fat32:
        return QCoreApplication::translate("usbrestore::Disk", "FAT32 (older devices)");
    case FileSystemType::Ntfs:
        return QCoreApplication::translate("usbrestore::Disk", "NTFS");
    case FileSystemType::Ext4:
        return QCoreApplication::translate("usbrestore::Disk", "ext4 (Linux)");
    }
    return QCoreApplication::translate("usbrestore::Disk", "Unknown");
}

QString fileSystemTypeToken(FileSystemType type)
{
    switch (type) {
    case FileSystemType::ExFat:
        return QStringLiteral("exfat");
    case FileSystemType::Fat32:
        return QStringLiteral("fat32");
    case FileSystemType::Ntfs:
        return QStringLiteral("ntfs");
    case FileSystemType::Ext4:
        return QStringLiteral("ext4");
    }
    return {};
}

bool parseFileSystemType(const QString &token, FileSystemType *type)
{
    FileSystemType parsed = FileSystemType::ExFat;
    if (token == QLatin1String("exfat")) {
        parsed = FileSystemType::ExFat;
    } else if (token == QLatin1String("fat32")) {
        parsed = FileSystemType::Fat32;
    } else if (token == QLatin1String("ntfs")) {
        parsed = FileSystemType::Ntfs;
    } else if (token == QLatin1String("ext4")) {
        parsed = FileSystemType::Ext4;
    } else {
        return false;
    }
    if (type) {
        *type = parsed;
    }
    return true;
}

QString describeDisk(const DiskInfo &disk, TextMode mode)
{
    const QString name = disk.name.trimmed().isEmpty()
                             ? coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "USB disk"), mode)
                             : disk.name.trimmed();
    const QString identifier = disk.displayId.isEmpty() ? disk.deviceId : disk.displayId;
    return QStringLiteral("%1 — %2 (%3)").arg(identifier, name, formatByteSize(disk.size, mode));
}

} // namespace usbrestore

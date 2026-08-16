#include "core/disk.h"

#include <QtGlobal>

namespace usbrestore {

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
        return QStringLiteral("%1 %2").arg(static_cast<qulonglong>(bytes)).arg(QLatin1String(units[unit]));
    }
    return QStringLiteral("%1 %2").arg(value, 0, 'f', 2).arg(QLatin1String(units[unit]));
}

QString partitionStyleName(quint16 style)
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
    return QStringLiteral("RAW / unknown");
}

QString healthStatusName(quint16 status)
{
    switch (static_cast<HealthStatus>(status)) {
    case HealthStatus::Healthy:
        return QStringLiteral("Healthy");
    case HealthStatus::Warning:
        return QStringLiteral("Warning");
    case HealthStatus::Unhealthy:
        return QStringLiteral("Unhealthy");
    }
    return QStringLiteral("Unknown");
}

QString busTypeName(quint32 busType)
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
        return QStringLiteral("Unknown");
    }
}

QString partitionStyleLabel(PartitionStyle style)
{
    switch (style) {
    case PartitionStyle::Gpt:
        return QStringLiteral("GPT");
    case PartitionStyle::Mbr:
        return QStringLiteral("MBR");
    case PartitionStyle::Unknown:
        break;
    }
    return QStringLiteral("Unknown");
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
        return QStringLiteral("exFAT (recommended)");
    case FileSystemType::Fat32:
        return QStringLiteral("FAT32 (older devices)");
    case FileSystemType::Ntfs:
        return QStringLiteral("NTFS");
    case FileSystemType::Ext4:
        return QStringLiteral("ext4 (Linux)");
    }
    return QStringLiteral("Unknown");
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

QString describeDisk(const DiskInfo &disk)
{
    const QString name = disk.name.trimmed().isEmpty() ? QStringLiteral("USB disk") : disk.name.trimmed();
    const QString identifier = disk.displayId.isEmpty() ? disk.deviceId : disk.displayId;
    return QStringLiteral("%1 — %2 (%3)").arg(identifier, name, formatByteSize(disk.size));
}

} // namespace usbrestore

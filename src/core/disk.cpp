#include "core/disk.h"

#include <QCoreApplication>
#include <QtGlobal>

namespace usbrestore {

namespace {

// One row per filesystem, so the display name, the drop-down label, the wire token
// and the parser cannot drift apart.
struct FileSystemNames {
    FileSystemType type;
    const char *name;
    const char *label;
    const char *token;
};

constexpr FileSystemNames FileSystemNamesTable[] = {
    {FileSystemType::ExFat, "exFAT", "exFAT (recommended)", "exfat"},
    {FileSystemType::Fat32, "FAT32", "FAT32 (older devices)", "fat32"},
    {FileSystemType::Ntfs, "NTFS", "NTFS", "ntfs"},
    {FileSystemType::Ext4, "ext4", "ext4 (Linux)", "ext4"},
};

// MSFT_Disk.BusType values; only the ones a USB stick can plausibly report plus the
// neighbours Windows documents.
struct BusTypeName {
    quint32 value;
    const char *name;
};

constexpr BusTypeName BusTypeNames[] = {
    {1, "SCSI"},
    {2, "ATAPI"},
    {3, "ATA"},
    {4, "1394"},
    {5, "SSA"},
    {6, "Fibre Channel"},
    {UsbBusType, "USB"},
    {8, "RAID"},
    {9, "iSCSI"},
    {10, "SAS"},
    {11, "SATA"},
    {12, "SD"},
    {13, "MMC"},
    {17, "NVMe"},
};

} // namespace

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

QString busTypeName(quint32 busType, TextMode mode)
{
    for (const BusTypeName &entry : BusTypeNames) {
        if (entry.value == busType) {
            return QString::fromLatin1(entry.name);
        }
    }
    return coreText(QT_TRANSLATE_NOOP("usbrestore::Core", "Unknown"), mode);
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
    for (const FileSystemNames &entry : FileSystemNamesTable) {
        if (entry.type == type) {
            return QString::fromLatin1(entry.name);
        }
    }
    return QStringLiteral("Unknown");
}

QString fileSystemTypeLabel(FileSystemType type)
{
    for (const FileSystemNames &entry : FileSystemNamesTable) {
        if (entry.type == type) {
            return QCoreApplication::translate("usbrestore::Disk", entry.label);
        }
    }
    return QCoreApplication::translate("usbrestore::Disk", "Unknown");
}

QString fileSystemTypeToken(FileSystemType type)
{
    for (const FileSystemNames &entry : FileSystemNamesTable) {
        if (entry.type == type) {
            return QString::fromLatin1(entry.token);
        }
    }
    return {};
}

bool parseFileSystemType(const QString &token, FileSystemType *type)
{
    for (const FileSystemNames &entry : FileSystemNamesTable) {
        if (token == QLatin1String(entry.token)) {
            if (type) {
                *type = entry.type;
            }
            return true;
        }
    }
    return false;
}

bool isKnownFileSystem(FileSystemType type)
{
    for (const FileSystemNames &entry : FileSystemNamesTable) {
        if (entry.type == type) {
            return true;
        }
    }
    return false;
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

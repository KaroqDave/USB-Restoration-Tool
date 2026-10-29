#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>
#include <optional>

namespace usbrestore {

// MSFT_Disk.BusType for USB; the Linux backend reports the same value so the
// safety rules stay platform-neutral.
inline constexpr quint32 UsbBusType = 7;

enum class PartitionStyle : quint16 {
    Unknown = 0,
    Mbr = 1,
    Gpt = 2,
};

enum class FileSystemType : quint16 {
    ExFat = 0,
    Fat32 = 1,
    Ntfs = 2,
    Ext4 = 3,
};

enum class HealthStatus : quint16 {
    Healthy = 0,
    Warning = 1,
    Unhealthy = 2,
};

// Both read fresh identity, state and mount points; restore checks need no filesystem display details.
enum class DiskDetails {
    Display,
    Restore,
};

struct DiskInfo {
    // "\\.\PhysicalDrive2" or "/dev/sdb": the disk's identity; every other field
    // is only corroboration.
    QString deviceId;

    // "Disk 2" (as Disk Management shows it) or "/dev/sdb"; Linux has no stable number.
    QString displayId;

    // Windows backend only; never used by the core.
    quint32 number = 0;

    quint32 busType = 0;
    QString name;
    QString uniqueId;
    QString serialNumber;
    QString path;
    std::optional<std::uint64_t> deviceSequence;
    std::uint64_t size = 0;
    quint32 sectorSize = 512;
    quint16 health = static_cast<quint16>(HealthStatus::Healthy);
    quint16 partitionStyle = static_cast<quint16>(PartitionStyle::Unknown);

    // "E:", "/run/media/dave/USB"
    QStringList mountPoints;
    QStringList labels;

    // As the platform names them ("exFAT", "vfat"); display only, no safety rule reads it.
    QStringList fileSystems;

    bool isBoot = false;
    bool isSystem = false;
    bool isReadOnly = false;
    bool isOffline = false;
};

QString formatByteSize(std::uint64_t bytes);
QString partitionStyleName(quint16 style);
QString partitionStyleLabel(PartitionStyle style);
QString fileSystemTypeName(FileSystemType type);
QString fileSystemTypeLabel(FileSystemType type);
QString fileSystemTypeToken(FileSystemType type);
bool parseFileSystemType(const QString &token, FileSystemType *type = nullptr);
QString healthStatusName(quint16 status);
QString busTypeName(quint32 busType);

QString describeDisk(const DiskInfo &disk);

} // namespace usbrestore

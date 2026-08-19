#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>

namespace usbrestore {

// The bus code for USB. On Windows this is MSFT_Disk.BusType; the Linux
// backend reports the same value when the device sits behind a USB controller,
// so the safety rules do not have to know which platform they are running on.
inline constexpr quint32 UsbBusType = 7;

// How a disk's partitions are described. Doubles as the style a restore is
// asked to produce, minus Unknown.
enum class PartitionStyle : quint16 {
    Unknown = 0,
    Mbr = 1,
    Gpt = 2,
};

// The filesystem a restore writes. Backends advertise which of these they can
// actually format; the GUI never assumes the OS from this list.
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

struct DiskInfo {
    // The device the restore reopens and re-verifies against:
    // "\\\\.\\PhysicalDrive2" on Windows, "/dev/sdb" on Linux. This is the
    // disk's identity as far as the core is concerned; everything else is
    // corroboration.
    QString deviceId;

    // What the disk is called on screen: "Disk 2" on Windows, "/dev/sdb" on
    // Linux. Windows disk numbers are meaningful to users because that is what
    // Disk Management shows; Linux has no equivalent number, and inventing one
    // would be inventing a number that changes when another stick is unplugged.
    QString displayId;

    // Windows disk number, used by the Windows backend for its IOCTLs and WQL
    // filters. Not meaningful on Linux and never used by the core.
    quint32 number = 0;

    quint32 busType = 0;
    QString name;
    QString uniqueId;
    QString serialNumber;
    QString path;
    std::uint64_t size = 0;
    quint32 sectorSize = 512;
    quint16 health = static_cast<quint16>(HealthStatus::Healthy);
    quint16 partitionStyle = static_cast<quint16>(PartitionStyle::Unknown);

    // Where the disk's volumes are currently reachable: "E:" on Windows,
    // "/run/media/dave/USB" on Linux.
    QStringList mountPoints;
    QStringList labels;

    // The filesystems found on the disk, as the platform names them: "exFAT"
    // from Windows, "vfat" or "iso9660" from udev on Linux. Shown to the user
    // so they can tell what a disk holds; no safety rule reads it.
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

// A one-line description of the disk for lists and confirmation prompts.
QString describeDisk(const DiskInfo &disk);

} // namespace usbrestore

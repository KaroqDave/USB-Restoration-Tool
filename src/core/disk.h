#pragma once

#include <QString>
#include <QStringList>

#include <cstdint>

namespace usbrestore {

// MSFT_Disk.BusType for USB. Every other bus is a disk this tool must not
// touch, so the value appears in the WQL filter, in the safety check, and in
// the post-open check against the device the handle actually points at.
inline constexpr quint32 UsbBusType = 7;

// MSFT_Disk.PartitionStyle and MSFT_Disk.HealthStatus are uint16 enums, not
// strings; Windows reports the code and the caller names it.
enum class PartitionStyle : quint16 {
    Unknown = 0,
    Mbr = 1,
    Gpt = 2,
};

enum class HealthStatus : quint16 {
    Healthy = 0,
    Warning = 1,
    Unhealthy = 2,
};

struct DiskInfo {
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
    QStringList driveLetters;
    QStringList labels;
    bool isBoot = false;
    bool isSystem = false;
    bool isReadOnly = false;
    bool isOffline = false;
};

QString formatByteSize(std::uint64_t bytes);
QString partitionStyleName(quint16 style);
QString healthStatusName(quint16 status);
QString busTypeName(quint32 busType);

// A one-line description of the disk for lists and confirmation prompts.
QString describeDisk(const DiskInfo &disk);

} // namespace usbrestore

#pragma once

#include "core/disk.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace usbrestore {

// The only identifier that ties a mountinfo entry back to its block device.
struct DeviceNumber {
    quint32 major = 0;
    quint32 minor = 0;

    bool isValid() const { return major != 0 || minor != 0; }
    bool operator==(const DeviceNumber &other) const { return major == other.major && minor == other.minor; }
};

struct MountEntry {
    DeviceNumber device;
    QString mountPoint;
    QString source;
    bool readOnly = false;
};

// Empty when the attribute does not exist, sysfs's way of saying "not applicable".
QString readSysfsAttribute(const QString &path);

// Kernel names ("sda", "mmcblk0"), without virtual and optical devices.
QStringList listBlockDeviceNames();

// Kernel names ("sda1", "mmcblk0p1").
QStringList listPartitionNames(const QString &diskName);

DeviceNumber readDeviceNumber(const QString &sysfsPath);

QVector<MountEntry> readMounts();

// Reads the bus path from the sysfs device link, as udisks2 does for ConnectionBus "usb".
bool isUsbBlockDevice(const QString &diskName);

// Prefers the "usb-" form, which carries vendor, product and serial.
QString stableDeviceIdLink(const QString &diskName);

QString labelForPartition(const QString &partitionName);

// "vfat" is reported as its FAT variant ("FAT32"). Display only, from udev's
// possibly stale record: the restore never trusts it. Empty when unknown.
QString fileSystemTypeForDevice(const QString &sysfsPath);

// "sda1" -> "sda", "nvme0n1p2" -> "nvme0n1"; empty when the name is not a partition.
QString parentDiskName(const QString &partitionName);

// Falls back to udev's record when the node cannot be opened, the usual case for the
// unprivileged GUI.
PartitionStyle detectPartitionStyle(const QString &devicePath, quint32 sectorSize);

} // namespace usbrestore

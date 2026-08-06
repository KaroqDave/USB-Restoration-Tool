#pragma once

#include "core/disk.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <cstdint>

namespace usbrestore {

// A device's kernel device number, which is the only identifier that ties a
// mount in /proc/self/mountinfo back to the block device it came from. Mount
// entries name a major:minor pair, not a path.
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

// Reads a sysfs attribute, trimmed. Returns an empty string when the attribute
// does not exist, which for sysfs is the normal way of saying "not applicable
// to this device".
QString readSysfsAttribute(const QString &path);

// Every whole disk under /sys/block, by kernel name ("sda", "mmcblk0"),
// excluding the virtual devices that are never restore targets: loop, ram,
// device-mapper, MD arrays, zram, network block devices and optical drives.
QStringList listBlockDeviceNames();

// The partitions of a disk, by kernel name ("sda1", "mmcblk0p1").
QStringList listPartitionNames(const QString &diskName);

DeviceNumber readDeviceNumber(const QString &sysfsPath);

// Everything currently mounted, from /proc/self/mountinfo.
QVector<MountEntry> readMounts();

// Whether the device sits behind a USB controller. Derived from the sysfs
// device link, which spells out the whole bus path from the PCI root down —
// the same thing udisks2 reads to report a ConnectionBus of "usb".
bool isUsbBlockDevice(const QString &diskName);

// The /dev/disk/by-id symlink that names this disk, preferring the "usb-"
// form, which carries the vendor, product and serial the device reported.
QString stableDeviceIdLink(const QString &diskName);

// The filesystem label of a partition, from /dev/disk/by-label.
QString labelForPartition(const QString &partitionName);

// The disk a partition belongs to, or an empty string when the name is not a
// partition. "sda1" -> "sda", "nvme0n1p2" -> "nvme0n1".
QString parentDiskName(const QString &partitionName);

// Reads the first sectors of a device to see which partition table, if any, is
// already on it. Falls back to what udev recorded when the device node cannot
// be opened, which is the ordinary case for the unprivileged GUI.
PartitionStyle detectPartitionStyle(const QString &devicePath, quint32 sectorSize);

} // namespace usbrestore

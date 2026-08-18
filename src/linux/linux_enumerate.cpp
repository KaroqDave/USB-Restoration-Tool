#include "linux/linux_enumerate.h"

#include "core/partition_table.h"
#include "core/safety.h"
#include "linux/sysfs.h"

#include <QDir>
#include <QFileInfo>

namespace usbrestore {

namespace {

// sysfs always reports sizes in 512-byte units regardless of the device's real
// sector size. Multiplying by the logical sector size instead is a mistake that
// silently quadruples the size of every 4 Kn disk.
constexpr std::uint64_t SysfsSectorUnit = 512;

} // namespace

const QStringList &protectedSystemMounts()
{
    // The filesystems a running Linux system is built out of. A USB disk that
    // has somehow ended up hosting one of these is not a restore target.
    static const QStringList mounts = {
        QStringLiteral("/"),
        QStringLiteral("/boot"),
        QStringLiteral("/boot/efi"),
        QStringLiteral("/efi"),
        QStringLiteral("/etc"),
        QStringLiteral("/home"),
        QStringLiteral("/nix"),
        QStringLiteral("/opt"),
        QStringLiteral("/srv"),
        QStringLiteral("/usr"),
        QStringLiteral("/var"),
    };
    return mounts;
}

QString devicePathFor(const QString &kernelName)
{
    return QStringLiteral("/dev/%1").arg(kernelName);
}

DiskInfo diskInfoFor(const QString &diskName)
{
    const QString sysfs = QStringLiteral("/sys/block/%1").arg(diskName);

    DiskInfo disk;
    disk.deviceId = devicePathFor(diskName);
    disk.displayId = disk.deviceId;
    disk.busType = isUsbBlockDevice(diskName) ? UsbBusType : 0;

    const QString vendor = readSysfsAttribute(QStringLiteral("%1/device/vendor").arg(sysfs));
    const QString model = readSysfsAttribute(QStringLiteral("%1/device/model").arg(sysfs));
    disk.name = QStringLiteral("%1 %2").arg(vendor, model).trimmed();
    if (disk.name.isEmpty()) {
        disk.name = QStringLiteral("USB disk");
    }

    disk.uniqueId = stableDeviceIdLink(diskName);
    disk.serialNumber = serialFromUsbByIdLink(disk.uniqueId, disk.name);
    disk.path = QFileInfo(sysfs).canonicalFilePath();

    disk.size = readSysfsAttribute(QStringLiteral("%1/size").arg(sysfs)).toULongLong() * SysfsSectorUnit;
    disk.sectorSize = readSysfsAttribute(QStringLiteral("%1/queue/logical_block_size").arg(sysfs)).toUInt();
    if (disk.sectorSize == 0) {
        disk.sectorSize = 512;
    }
    disk.isReadOnly = readSysfsAttribute(QStringLiteral("%1/ro").arg(sysfs)) == QStringLiteral("1");
    disk.isOffline = disk.size == 0;
    disk.health = static_cast<quint16>(HealthStatus::Healthy);
    disk.partitionStyle = static_cast<quint16>(detectPartitionStyle(disk.deviceId, disk.sectorSize));

    // Mount points and labels come from the partitions, plus the whole-disk
    // device itself for a stick written with a bare filesystem and no table.
    const QVector<MountEntry> mounts = readMounts();
    const DeviceNumber diskNumber = readDeviceNumber(sysfs);
    QVector<DeviceNumber> owned;
    if (diskNumber.isValid()) {
        owned.append(diskNumber);
    }
    for (const QString &partition : listPartitionNames(diskName)) {
        const DeviceNumber number = readDeviceNumber(QStringLiteral("%1/%2").arg(sysfs, partition));
        if (number.isValid()) {
            owned.append(number);
        }
        const QString label = labelForPartition(partition);
        if (!label.isEmpty() && !disk.labels.contains(label)) {
            disk.labels.append(label);
        }
    }

    for (const MountEntry &mount : mounts) {
        if (owned.contains(mount.device) && !disk.mountPoints.contains(mount.mountPoint)) {
            disk.mountPoints.append(mount.mountPoint);
        }
    }

    return disk;
}

QVector<DiskInfo> listUsbDiskInfos(QString *error)
{
    QVector<DiskInfo> disks;
    const QStringList names = listBlockDeviceNames();
    if (names.isEmpty() && !QDir(QStringLiteral("/sys/block")).exists()) {
        if (error) {
            *error = QStringLiteral("/sys/block is not available, so block devices cannot be listed.");
        }
        return disks;
    }

    for (const QString &name : names) {
        if (!isUsbBlockDevice(name)) {
            continue;
        }
        disks.append(diskInfoFor(name));
    }
    return disks;
}

bool refreshUsbDisk(const QString &deviceId, DiskInfo *current, QString *error)
{
    const QString name = QFileInfo(deviceId).fileName();
    if (name.isEmpty() || !QDir(QStringLiteral("/sys/block/%1").arg(name)).exists()) {
        if (error) {
            *error = QStringLiteral("%1 is no longer present. Refresh and select the USB again.").arg(deviceId);
        }
        return false;
    }
    if (!isUsbBlockDevice(name)) {
        if (error) {
            *error = QStringLiteral("%1 is no longer reported on the USB bus. Refresh and select the USB again.")
                         .arg(deviceId);
        }
        return false;
    }

    if (current) {
        *current = diskInfoFor(name);
    }
    return true;
}

RestoreGuard linuxRestoreGuard()
{
    RestoreGuard guard;
    guard.mountPoints = protectedSystemMounts();

    // Anything actually mounted at a protected path pins the disk it lives on,
    // so a stick that somehow backs / cannot be selected even if its mount
    // point is spelled differently than the list above.
    const QVector<MountEntry> mounts = readMounts();
    for (const MountEntry &mount : mounts) {
        if (!isProtectedMountPoint(mount.mountPoint, guard.mountPoints)) {
            continue;
        }
        if (!mount.source.startsWith(QStringLiteral("/dev/"))) {
            continue;
        }

        const QString name = QFileInfo(mount.source).fileName();
        const QString disk = parentDiskName(name);
        const QString deviceId = devicePathFor(disk.isEmpty() ? name : disk);
        if (!guard.deviceIds.contains(deviceId)) {
            guard.deviceIds.append(deviceId);
        }
    }

    return guard;
}

} // namespace usbrestore

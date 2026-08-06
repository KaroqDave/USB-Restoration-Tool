#include "linux/linux_disk_service.h"

#include "core/partition_table.h"
#include "linux/block_device.h"
#include "linux/sysfs.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QRandomGenerator>
#include <QRegularExpression>
#include <QThread>

#include <sys/mount.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <memory>

namespace usbrestore {

namespace {

// sysfs always reports sizes in 512-byte units regardless of the device's real
// sector size. Multiplying by the logical sector size instead is a mistake that
// silently quadruples the size of every 4 Kn disk.
constexpr std::uint64_t SysfsSectorUnit = 512;

constexpr std::uint64_t SignatureClearBytes = 4ull * 1024ull * 1024ull;
constexpr int NewPartitionTimeoutMs = 30 * 1000;

// The filesystems a running Linux system is built out of. A USB disk that has
// somehow ended up hosting one of these is not a restore target.
const QStringList &protectedSystemMounts()
{
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

// mkfs is looked up in a fixed set of system directories rather than through
// $PATH. This process runs as root, usually reached through sudo, and $PATH is
// exactly the kind of thing an unprivileged caller can arrange to control.
QString findSystemTool(const QString &name)
{
    static const QStringList directories = {
        QStringLiteral("/usr/sbin"),
        QStringLiteral("/sbin"),
        QStringLiteral("/usr/bin"),
        QStringLiteral("/bin"),
        QStringLiteral("/usr/local/sbin"),
        QStringLiteral("/usr/local/bin"),
    };

    for (const QString &directory : directories) {
        const QString candidate = QStringLiteral("%1/%2").arg(directory, name);
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable() && !info.isDir()) {
            return candidate;
        }
    }
    return {};
}

QByteArray randomGuid()
{
    quint32 words[4] = {};
    QRandomGenerator::system()->fillRange(words);
    QByteArray guid(reinterpret_cast<const char *>(words), 16);
    // RFC 4122 version 4, variant 1. GPT does not check, but a well-formed
    // UUID is what every other tool on the disk will expect to read back.
    guid[7] = static_cast<char>((guid.at(7) & 0x0F) | 0x40);
    guid[8] = static_cast<char>((guid.at(8) & 0x3F) | 0x80);
    return guid;
}

// udev names a USB disk "usb-<vendor>_<product>_<serial>-0:0". The trailing
// "-<host>:<lun>" is udev's own, and the serial is whatever follows the last
// underscore once it is gone.
QString serialFromByIdLink(const QString &link)
{
    if (!link.startsWith(QStringLiteral("usb-"))) {
        return {};
    }

    QString value = link;
    static const QRegularExpression hostSuffix(QStringLiteral("-[0-9]+:[0-9]+$"));
    value.remove(hostSuffix);

    const int lastSeparator = value.lastIndexOf(QLatin1Char('_'));
    return lastSeparator > 0 ? value.mid(lastSeparator + 1) : QString();
}

QString devicePathFor(const QString &kernelName)
{
    return QStringLiteral("/dev/%1").arg(kernelName);
}

// "sdb" + 1 -> "sdb1", but "mmcblk0" + 1 -> "mmcblk0p1" and "nvme0n1" + 1 ->
// "nvme0n1p1": a trailing digit means the partition number needs a "p" to keep
// it from running into the device name.
QString partitionNameFor(const QString &diskName, int index)
{
    const bool needsSeparator = !diskName.isEmpty() && diskName.back().isDigit();
    return QStringLiteral("%1%2%3").arg(diskName, needsSeparator ? QStringLiteral("p") : QString()).arg(index);
}

QString describeErrno(const QString &context, int errorNumber)
{
    return QStringLiteral("%1: %2 (errno %3)")
        .arg(context, QString::fromLocal8Bit(std::strerror(errorNumber)))
        .arg(errorNumber);
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
    disk.serialNumber = serialFromByIdLink(disk.uniqueId);
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

} // namespace

std::unique_ptr<DiskService> DiskService::create()
{
    return std::make_unique<LinuxDiskService>();
}

bool LinuxDiskService::isPrivileged() const
{
    return ::geteuid() == 0;
}

QString LinuxDiskService::privilegeHint() const
{
    return QStringLiteral("USB Restoration Tool needs root permission to restore USB disks.\n\nClose this window and "
                          "start it again with sudo, for example:\n\n    sudo %1")
        .arg(QCoreApplication::applicationFilePath());
}

QVector<DiskInfo> LinuxDiskService::listUsbDisks(QString *error) const
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

bool LinuxDiskService::refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error) const
{
    const QString name = QFileInfo(disk.deviceId).fileName();
    if (name.isEmpty() || !QDir(QStringLiteral("/sys/block/%1").arg(name)).exists()) {
        if (error) {
            *error = QStringLiteral("%1 is no longer present. Refresh and select the USB again.").arg(disk.deviceId);
        }
        return false;
    }
    if (!isUsbBlockDevice(name)) {
        if (error) {
            *error = QStringLiteral("%1 is no longer reported on the USB bus. Refresh and select the USB again.")
                         .arg(disk.deviceId);
        }
        return false;
    }

    if (current) {
        *current = diskInfoFor(name);
    }
    return true;
}

RestoreGuard LinuxDiskService::restoreGuard() const
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

int LinuxDiskService::totalRestoreSteps() const
{
    return 10;
}

QString LinuxDiskService::restoredLocationNoun() const
{
    return QStringLiteral("device");
}

bool LinuxDiskService::unmountDisk(const DiskInfo &disk, RestoreReporter &reporter, QString *error) const
{
    const QString diskName = QFileInfo(disk.deviceId).fileName();
    const QVector<MountEntry> mounts = readMounts();

    QVector<DeviceNumber> owned;
    const DeviceNumber diskNumber = readDeviceNumber(QStringLiteral("/sys/block/%1").arg(diskName));
    if (diskNumber.isValid()) {
        owned.append(diskNumber);
    }
    for (const QString &partition : listPartitionNames(diskName)) {
        const DeviceNumber number = readDeviceNumber(QStringLiteral("/sys/block/%1/%2").arg(diskName, partition));
        if (number.isValid()) {
            owned.append(number);
        }
    }

    // Deepest mount point first, so a filesystem mounted inside another one on
    // the same disk is released before the one it sits in.
    QVector<MountEntry> ordered = mounts;
    std::sort(ordered.begin(), ordered.end(), [](const MountEntry &left, const MountEntry &right) {
        return left.mountPoint.size() > right.mountPoint.size();
    });

    for (const MountEntry &mount : ordered) {
        if (!owned.contains(mount.device)) {
            continue;
        }
        // Nothing on this disk should be a protected path, but the check that
        // guarantees it ran against a snapshot. This is the same question asked
        // of the mount table as it is right now, immediately before unmounting.
        if (isProtectedMountPoint(mount.mountPoint, protectedSystemMounts())) {
            if (error) {
                *error = QStringLiteral("%1 is mounted at %2, which the running system depends on.")
                             .arg(disk.deviceId, mount.mountPoint);
            }
            return false;
        }

        reporter.detail(QStringLiteral("Unmounting %1").arg(mount.mountPoint));
        if (::umount2(mount.mountPoint.toLocal8Bit().constData(), 0) != 0) {
            const int errorNumber = errno;
            if (errorNumber == EINVAL || errorNumber == ENOENT) {
                // Already gone between reading the table and acting on it.
                continue;
            }
            if (error) {
                *error = describeErrno(QStringLiteral("Could not unmount %1. Close anything using the drive and try "
                                                      "again")
                                           .arg(mount.mountPoint),
                                       errorNumber);
            }
            return false;
        }
    }

    return true;
}

bool LinuxDiskService::formatExFat(const QString &partitionPath,
                                   const QString &label,
                                   RestoreReporter &reporter,
                                   QString *error) const
{
    QString tool = findSystemTool(QStringLiteral("mkfs.exfat"));
    if (tool.isEmpty()) {
        tool = findSystemTool(QStringLiteral("mkexfatfs"));
    }
    if (tool.isEmpty()) {
        if (error) {
            *error = QStringLiteral("mkfs.exfat was not found. Install exfatprogs (Debian/Ubuntu and Fedora: "
                                    "\"exfatprogs\", Arch: \"exfatprogs\") and try again.");
        }
        return false;
    }

    // exfatprogs takes -L for the label; the older exfat-utils took -n. Which
    // one is installed is not worth asking the user about, so the second form
    // is tried when the first is rejected *as an unknown option* — and only
    // then. Retrying a format that failed for a real reason would just run
    // mkfs twice and report the wrong cause.
    const QList<QStringList> attempts = {
        {QStringLiteral("-L"), label, partitionPath},
        {QStringLiteral("-n"), label, partitionPath},
    };

    QString lastOutput;
    for (const QStringList &arguments : attempts) {
        QProcess process;
        process.setProgram(tool);
        process.setArguments(arguments);
        process.setProcessChannelMode(QProcess::MergedChannels);
        process.start();
        if (!process.waitForStarted(10 * 1000)) {
            if (error) {
                *error = QStringLiteral("Could not run %1.").arg(tool);
            }
            return false;
        }
        if (!process.waitForFinished(15 * 60 * 1000)) {
            process.kill();
            process.waitForFinished(5 * 1000);
            if (error) {
                *error = QStringLiteral("%1 did not finish within 15 minutes.").arg(tool);
            }
            return false;
        }

        lastOutput = QString::fromLocal8Bit(process.readAll()).trimmed();
        if (process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0) {
            reporter.detail(QStringLiteral("%1 %2").arg(tool, arguments.join(QLatin1Char(' '))));
            if (!lastOutput.isEmpty()) {
                reporter.detail(lastOutput);
            }
            return true;
        }

        reporter.detail(QStringLiteral("%1 %2 failed: %3").arg(tool, arguments.join(QLatin1Char(' ')), lastOutput));

        static const QStringList unknownOptionMarkers = {
            QStringLiteral("invalid option"),
            QStringLiteral("unrecognized option"),
            QStringLiteral("unknown option"),
            QStringLiteral("illegal option"),
        };
        bool looksLikeWrongFlag = false;
        for (const QString &marker : unknownOptionMarkers) {
            if (lastOutput.contains(marker, Qt::CaseInsensitive)) {
                looksLikeWrongFlag = true;
                break;
            }
        }
        if (!looksLikeWrongFlag) {
            break;
        }
    }

    if (error) {
        *error = lastOutput.isEmpty() ? QStringLiteral("%1 failed to format %2.").arg(tool, partitionPath)
                                      : QStringLiteral("%1 failed to format %2: %3")
                                            .arg(tool, partitionPath, lastOutput);
    }
    return false;
}

bool LinuxDiskService::restore(const RestoreRequest &request,
                               RestoreReporter &reporter,
                               RestoreResult *result,
                               QString *error)
{
    DiskInfo disk = request.disk;
    QString reason;
    if (!isSafeRestoreTarget(disk, request.guard, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }

    reporter.step(QStringLiteral("Verifying the selected USB disk"));
    DiskInfo currentDisk;
    if (!refreshDisk(disk, &currentDisk, error)) {
        return false;
    }
    if (!isSameRestoreTarget(disk, currentDisk, &reason) ||
        !isSafeRestoreTarget(currentDisk, request.guard, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    disk = currentDisk;

    // Identified before anything is unmounted, so a wrong device is caught
    // while the only thing that has happened is a read-only open.
    reporter.step(QStringLiteral("Identifying the block device"));
    {
        BlockDevice inspect(disk.deviceId);
        if (!inspect.open(BlockDevice::Access::Inspect, error) || !inspect.verifyIdentity(disk, error)) {
            return false;
        }
    }

    if (reporter.cancelRequested()) {
        return false;
    }

    reporter.step(QStringLiteral("Unmounting existing filesystems"));
    if (!unmountDisk(disk, reporter, error)) {
        return false;
    }

    // O_EXCL is the real gate: the kernel refuses it while any partition of the
    // disk is still mounted or claimed, so reaching this point proves nothing
    // is live underneath the write that follows.
    reporter.step(QStringLiteral("Opening the device exclusively"));
    BlockDevice device(disk.deviceId);
    if (!device.open(BlockDevice::Access::Exclusive, error)) {
        return false;
    }
    if (!device.verifyIdentity(disk, error)) {
        return false;
    }

    if (reporter.cancelRequested()) {
        return false;
    }

    PartitionTableRequest tableRequest;
    tableRequest.style = request.style;
    tableRequest.diskSize = disk.size;
    tableRequest.sectorSize = disk.sectorSize;
    tableRequest.layout = calculateGptLayout(disk.size, disk.sectorSize);
    tableRequest.diskGuid = randomGuid();
    tableRequest.partitionGuid = randomGuid();
    tableRequest.partitionName = request.volumeLabel;
    if (!isWritablePartitionRequest(tableRequest, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }

    // Past this line nothing is reversible, so cancellation is no longer
    // checked: a half-written table is worse than a finished one.
    reporter.step(QStringLiteral("Clearing old MBR, GPT and ISO signatures"));
    const std::uint64_t sector = disk.sectorSize;
    const std::uint64_t clearBytes = (qMin(SignatureClearBytes, disk.size / 4) / sector) * sector;
    if (clearBytes < sector) {
        if (error) {
            *error = QStringLiteral("The disk is too small to clear safely.");
        }
        return false;
    }
    if (!device.writeZeros(0, clearBytes, disk.sectorSize, error) ||
        !device.writeZeros(((disk.size - clearBytes) / sector) * sector, clearBytes, disk.sectorSize, error)) {
        return false;
    }

    reporter.step(QStringLiteral("Writing the %1 partition table").arg(partitionStyleLabel(request.style)));
    if (request.style == PartitionStyle::Gpt) {
        if (!device.writeAt(0, buildGptPrimary(tableRequest), error) ||
            !device.writeAt(gptBackupOffset(tableRequest), buildGptBackup(tableRequest), error)) {
            return false;
        }
    } else {
        if (!device.writeAt(0, buildMbr(tableRequest), error)) {
            return false;
        }
    }

    reporter.step(QStringLiteral("Re-reading the partition table"));
    if (!device.flush(error)) {
        return false;
    }
    if (!device.rereadPartitionTable(error)) {
        return false;
    }
    // The descriptor is closed before the kernel is asked for the new
    // partition: udev creates the node, and it will not while the disk is
    // still held open exclusively.
    device.close();

    reporter.step(QStringLiteral("Waiting for the new partition to appear"));
    const QString diskName = QFileInfo(disk.deviceId).fileName();
    const QString partitionPath = devicePathFor(partitionNameFor(diskName, 1));
    QElapsedTimer timer;
    timer.start();
    while (!QFileInfo::exists(partitionPath) && timer.elapsed() < NewPartitionTimeoutMs) {
        QThread::msleep(250);
    }
    if (!QFileInfo::exists(partitionPath)) {
        if (error) {
            *error = QStringLiteral("The kernel did not publish %1 within %2 seconds.")
                         .arg(partitionPath)
                         .arg(NewPartitionTimeoutMs / 1000);
        }
        return false;
    }

    reporter.step(QStringLiteral("Formatting as exFAT"));
    if (!formatExFat(partitionPath, request.volumeLabel, reporter, error)) {
        return false;
    }

    reporter.step(QStringLiteral("Restore complete"));
    if (result) {
        // Mounting is left to the desktop: on Linux that is the file manager's
        // job, and mounting as root would leave a volume the user cannot write
        // to without sudo.
        result->location = partitionPath;
    }
    return true;
}

} // namespace usbrestore

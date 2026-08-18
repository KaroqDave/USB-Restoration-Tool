#include "linux/linux_restore.h"

#include "core/partition_table.h"
#include "core/safety.h"
#include "linux/block_device.h"
#include "linux/linux_enumerate.h"
#include "linux/sysfs.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QList>
#include <QLocale>
#include <QProcess>
#include <QRandomGenerator>
#include <QStringList>
#include <QThread>

#include <pwd.h>
#include <sys/mount.h>
#include <sys/types.h>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <limits>
#include <optional>

namespace usbrestore {

namespace {

constexpr std::uint64_t SignatureClearBytes = 4ull * 1024ull * 1024ull;
constexpr int NewPartitionTimeoutMs = 30 * 1000;

// mkfs is looked up in a fixed set of system directories rather than through
// $PATH. This code runs as root, reached either through sudo or through pkexec,
// and $PATH is exactly the kind of thing an unprivileged caller can arrange to
// control.
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

bool unmountDisk(const DiskInfo &disk, RestoreReporter &reporter, QString *error)
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

// The account a finished ext4 volume should belong to. ext4 is the only
// filesystem offered here that stores POSIX ownership on the volume itself; for
// exFAT, FAT32 and NTFS the mount options decide, so the question does not
// arise. This code is root either way it was reached, so without this the new
// root directory is root:root 0755 and every write to the auto-mounted stick
// fails with EACCES.
struct DesktopOwner {
    quint32 uid = 0;
    quint32 gid = 0;
};

std::optional<quint32> environmentId(const char *name)
{
    const QByteArray raw = qgetenv(name);
    if (raw.isEmpty()) {
        return std::nullopt;
    }
    bool ok = false;
    const qulonglong value = QString::fromLatin1(raw).toULongLong(&ok);
    if (!ok || value > std::numeric_limits<quint32>::max()) {
        return std::nullopt;
    }
    return static_cast<quint32>(value);
}

// pkexec sets PKEXEC_UID and sudo sets SUDO_UID, and both do so in the
// privileged child's environment rather than passing along the caller's. That
// makes them facts about who asked rather than claims the caller wrote, which
// is the only reason this is allowed to read them at all. Worst case, a caller
// that somehow controls PKEXEC_UID names a different account to own a disk it
// was already erasing.
std::optional<DesktopOwner> desktopOwner()
{
    std::optional<quint32> uid = environmentId("PKEXEC_UID");
    if (!uid) {
        uid = environmentId("SUDO_UID");
    }
    if (!uid || *uid == 0) {
        // A root login, reached through neither. There is no unprivileged
        // account to name, and root:root is what mkfs would write anyway.
        return std::nullopt;
    }

    // The group is not in the environment under pkexec, so take the login group
    // the account actually has. SUDO_GID is a fallback for the case where that
    // lookup fails, which means an account with no passwd entry at all.
    if (const passwd *entry = ::getpwuid(static_cast<uid_t>(*uid))) {
        return DesktopOwner{*uid, static_cast<quint32>(entry->pw_gid)};
    }
    if (const std::optional<quint32> gid = environmentId("SUDO_GID")) {
        return DesktopOwner{*uid, *gid};
    }
    return std::nullopt;
}

bool formatPartition(FileSystemType fileSystem,
                     const QString &tool,
                     const QString &partitionPath,
                     const QString &label,
                     RestoreReporter &reporter,
                     QString *error)
{
    QList<QStringList> attempts;
    switch (fileSystem) {
    case FileSystemType::ExFat:
        // exfatprogs takes -L for the label; the older exfat-utils took -n.
        // Which one is installed is not worth asking the user about, so the
        // second form is tried when the first is rejected *as an unknown
        // option* — and only then. Retrying a format that failed for a real
        // reason would just run mkfs twice and report the wrong cause.
        attempts = {
            {QStringLiteral("-L"), label, partitionPath},
            {QStringLiteral("-n"), label, partitionPath},
        };
        break;
    case FileSystemType::Fat32:
        attempts = {{QStringLiteral("-F"), QStringLiteral("32"), QStringLiteral("-n"), label, partitionPath}};
        break;
    case FileSystemType::Ntfs:
        // -Q is a quick format; a full zero-fill of a 64 GB stick can take
        // hours and is not what anyone restoring a USB drive asked for.
        attempts = {
            {QStringLiteral("-Q"), QStringLiteral("-L"), label, partitionPath},
            {QStringLiteral("-f"), QStringLiteral("-L"), label, partitionPath},
        };
        break;
    case FileSystemType::Ext4: {
        // -F so mkfs does not ask; -m 0 so a USB stick does not reserve 5%
        // of itself for root.
        QStringList arguments = {QStringLiteral("-F"),
                                 QStringLiteral("-L"),
                                 label,
                                 QStringLiteral("-m"),
                                 QStringLiteral("0")};
        if (const std::optional<DesktopOwner> owner = desktopOwner()) {
            arguments << QStringLiteral("-E")
                      << QStringLiteral("root_owner=%1:%2").arg(owner->uid).arg(owner->gid);
            reporter.detail(
                QStringLiteral("New filesystem will belong to uid %1, gid %2").arg(owner->uid).arg(owner->gid));
        } else {
            reporter.detail(
                QStringLiteral("No unprivileged caller to give the new filesystem to; it will belong to root."));
        }
        arguments << partitionPath;
        attempts = {arguments};
        break;
    }
    }

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
        *error = lastOutput.isEmpty()
                     ? QStringLiteral("%1 failed to format %2.").arg(tool, partitionPath)
                     : QStringLiteral("%1 failed to format %2: %3").arg(tool, partitionPath, lastOutput);
    }
    return false;
}

QString resolveFormatTool(FileSystemType fileSystem, QString *error)
{
    const auto missing = [error](const QString &message) {
        if (error) {
            *error = message;
        }
        return QString();
    };

    switch (fileSystem) {
    case FileSystemType::ExFat: {
        QString tool = findSystemTool(QStringLiteral("mkfs.exfat"));
        if (tool.isEmpty()) {
            tool = findSystemTool(QStringLiteral("mkexfatfs"));
        }
        if (tool.isEmpty()) {
            return missing(QStringLiteral("mkfs.exfat was not found. Install exfatprogs (Debian/Ubuntu and Fedora: "
                                          "\"exfatprogs\", Arch: \"exfatprogs\") and try again."));
        }
        return tool;
    }
    case FileSystemType::Fat32: {
        QString tool = findSystemTool(QStringLiteral("mkfs.vfat"));
        if (tool.isEmpty()) {
            tool = findSystemTool(QStringLiteral("mkfs.fat"));
        }
        if (tool.isEmpty()) {
            return missing(QStringLiteral("mkfs.vfat was not found. Install dosfstools and try again."));
        }
        return tool;
    }
    case FileSystemType::Ntfs: {
        QString tool = findSystemTool(QStringLiteral("mkfs.ntfs"));
        if (tool.isEmpty()) {
            tool = findSystemTool(QStringLiteral("mkntfs"));
        }
        if (tool.isEmpty()) {
            return missing(QStringLiteral("mkfs.ntfs was not found. Install ntfs-3g (Debian/Ubuntu: \"ntfs-3g\", "
                                          "Fedora: \"ntfsprogs\", Arch: \"ntfs-3g\") and try again."));
        }
        return tool;
    }
    case FileSystemType::Ext4: {
        const QString tool = findSystemTool(QStringLiteral("mkfs.ext4"));
        if (tool.isEmpty()) {
            return missing(QStringLiteral("mkfs.ext4 was not found. Install e2fsprogs and try again."));
        }
        return tool;
    }
    }
    return missing(QStringLiteral("That filesystem is not supported."));
}

} // namespace

// Whether mkfs can be trusted to produce this filesystem on this disk, asked
// before anything is erased. Only FAT32 has anything to say: dosfstools refuses
// neither edge of its own range, so a volume outside it comes back as a
// successful exit and a wrong filesystem. See minimumMkfsFat32VolumeBytes().
bool canCreateFileSystemOn(FileSystemType fileSystem, const DiskInfo &disk, QString *reason)
{
    const auto refuse = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };

    switch (fileSystem) {
    case FileSystemType::ExFat:
    case FileSystemType::Ntfs:
    case FileSystemType::Ext4:
        return true;
    case FileSystemType::Fat32: {
        // The partition, not the disk: it is a megabyte shorter, and it is what
        // mkfs.vfat is pointed at.
        const std::uint64_t length = calculateGptLayout(disk.size, disk.sectorSize).length;
        const std::uint64_t maximum = maximumMkfsFat32VolumeBytes(disk.sectorSize);
        const std::uint64_t minimum = minimumMkfsFat32VolumeBytes(disk.sectorSize);
        if (maximum == 0 || minimum == 0) {
            return refuse(
                QStringLiteral("A disk with %1-byte sectors cannot be formatted as FAT32.").arg(disk.sectorSize));
        }
        if (length > maximum) {
            // mkfs.vfat would not refuse this. It clamps, warns, and leaves the
            // tail of the disk unreachable.
            return refuse(QStringLiteral("FAT32 cannot address a volume larger than %1 with %2-byte sectors, and "
                                         "the rest of the drive would be left unused. Choose exFAT or NTFS.")
                              .arg(QLocale().formattedDataSize(static_cast<qint64>(maximum)))
                              .arg(disk.sectorSize));
        }
        if (length < minimum) {
            // Nor this one: it warns that the cluster count is below FAT32's
            // minimum and writes the filesystem anyway.
            return refuse(QStringLiteral("This drive is too small for a valid FAT32 filesystem; it needs at least "
                                         "%1. Choose exFAT.")
                              .arg(QLocale().formattedDataSize(static_cast<qint64>(minimum))));
        }
        return true;
    }
    }
    return refuse(QStringLiteral("That filesystem is not supported."));
}

bool performLinuxRestore(const RestoreRequest &request,
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

    // Looked up before anything is overwritten, so a missing mkfs does not
    // leave a blank partition table behind.
    const QString formatTool = resolveFormatTool(request.fileSystem, error);
    if (formatTool.isEmpty()) {
        return false;
    }

    reporter.step(QStringLiteral("Verifying the selected USB disk"));
    DiskInfo currentDisk;
    if (!refreshUsbDisk(disk.deviceId, &currentDisk, error)) {
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

    // Asked against the disk this process just re-derived, not the one it was
    // handed. The GUI asks the same question before the acknowledgement dialog,
    // but a check that only ran there would be one a caller could skip by
    // starting the helper itself.
    if (!canCreateFileSystemOn(request.fileSystem, disk, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }

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
    //
    // The retry is for the desktop automounter. Unmounting a stick is exactly
    // the event that prompts GNOME or KDE to mount it straight back, and it can
    // win the race between the umount above and the open below. Fedora Media
    // Writer does not have this problem because UDisks2 both unmounts and holds
    // the device; doing it directly means handling the race directly.
    reporter.step(QStringLiteral("Opening the device exclusively"));
    BlockDevice device(disk.deviceId);
    QString openError;
    bool opened = false;
    for (int attempt = 0; attempt < 3 && !opened; ++attempt) {
        if (attempt > 0) {
            reporter.detail(QStringLiteral("Device was busy; unmounting again and retrying the exclusive open"));
            if (!unmountDisk(disk, reporter, error)) {
                return false;
            }
            QThread::msleep(500);
        }
        openError.clear();
        opened = device.open(BlockDevice::Access::Exclusive, &openError);
    }
    if (!opened) {
        if (error) {
            *error = openError;
        }
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
    tableRequest.fileSystem = request.fileSystem;
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

    // A brand new partition on a removable disk is precisely what a desktop
    // automounter jumps on, and mkfs refuses a mounted device. Give udev
    // a moment to do whatever it is going to do, then undo it.
    QThread::msleep(500);
    if (!unmountDisk(disk, reporter, error)) {
        return false;
    }

    reporter.step(QStringLiteral("Formatting as %1").arg(fileSystemTypeName(request.fileSystem)));
    if (!formatPartition(request.fileSystem, formatTool, partitionPath, request.volumeLabel, reporter, error)) {
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

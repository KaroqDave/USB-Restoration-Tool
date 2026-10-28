#include "linux/linux_restore.h"

#include "core/partition_table.h"
#include "core/safety.h"
#include "linux/block_device.h"
#include "linux/loop_device.h"
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

// Fixed directories, not $PATH: this runs as root and the caller may control $PATH.
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
    // RFC 4122 version 4, variant 1: GPT does not check, but other tools expect it.
    guid[7] = static_cast<char>((guid.at(7) & 0x0F) | 0x40);
    guid[8] = static_cast<char>((guid.at(8) & 0x3F) | 0x80);
    return guid;
}

// "sdb" -> "sdb1", but a name ending in a digit takes a "p": "nvme0n1" -> "nvme0n1p1".
QString partitionNameFor(const QString &diskName, int index)
{
    const bool needsSeparator = !diskName.isEmpty() && diskName.back().isDigit();
    return QStringLiteral("%1%2%3").arg(diskName, needsSeparator ? QStringLiteral("p") : QString()).arg(index);
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

    // Deepest first, so a nested mount is released before its parent.
    QVector<MountEntry> ordered = mounts;
    std::sort(ordered.begin(), ordered.end(), [](const MountEntry &left, const MountEntry &right) {
        return left.mountPoint.size() > right.mountPoint.size();
    });

    for (const MountEntry &mount : ordered) {
        if (!owned.contains(mount.device)) {
            continue;
        }
        // The guard ran against a snapshot; ask again of the live mount table.
        if (isProtectedMountPoint(mount.mountPoint, protectedSystemMounts())) {
            if (error) {
                *error = QStringLiteral(
                             "%1 is mounted at %2, which the running system needs. Pick a different "
                             "drive.")
                             .arg(disk.displayId.isEmpty() ? disk.deviceId : disk.displayId, mount.mountPoint);
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
                *error = deviceErrorText(QStringLiteral("Could not unmount %1").arg(mount.mountPoint), errorNumber);
            }
            return false;
        }
    }

    return true;
}

// Owner for a new ext4 root directory, which would otherwise be root:root 0755 and
// unwritable once auto-mounted. Other filesystems take ownership from mount options.
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

// pkexec and sudo set these themselves rather than passing the caller's environment,
// so they are facts, not claims. At worst a spoofed one picks the owner of a disk being erased.
std::optional<DesktopOwner> desktopOwner()
{
    std::optional<quint32> uid = environmentId("PKEXEC_UID");
    if (!uid) {
        uid = environmentId("SUDO_UID");
    }
    if (!uid || *uid == 0) {
        return std::nullopt;
    }

    // pkexec gives no gid; SUDO_GID covers an account with no passwd entry.
    if (const passwd *entry = ::getpwuid(static_cast<uid_t>(*uid))) {
        return DesktopOwner{*uid, static_cast<quint32>(entry->pw_gid)};
    }
    if (const std::optional<quint32> gid = environmentId("SUDO_GID")) {
        return DesktopOwner{*uid, *gid};
    }
    return std::nullopt;
}

} // namespace

bool formatLinuxPartition(
    FileSystemType fileSystem,
    const QString &tool,
    const QString &partitionPath,
    const QString &label,
    std::uint64_t partitionStartSector,
    RestoreReporter &reporter,
    QString *error)
{
    QList<QStringList> attempts;
    switch (fileSystem) {
    case FileSystemType::ExFat:
        // exfatprogs takes -L, exfat-utils -n. Retried only on an unknown-option
        // error, so a real failure is not masked by a second run.
        attempts = {
            {QStringLiteral("-L"), label, partitionPath},
            {QStringLiteral("-n"), label, partitionPath},
        };
        break;
    case FileSystemType::Fat32:
        attempts = {
            {QStringLiteral("-F"),
             QStringLiteral("32"),
             QStringLiteral("-n"),
             label,
             QStringLiteral("-h"),
             QString::number(partitionStartSector),
             partitionPath}};
        break;
    case FileSystemType::Ntfs:
        // -Q skips a zero-fill that takes hours. One form only: -f is the same flag, not a dialect.
        attempts = {
            {QStringLiteral("-Q"),
             QStringLiteral("-L"),
             label,
             QStringLiteral("-p"),
             QString::number(partitionStartSector),
             partitionPath}};
        break;
    case FileSystemType::Ext4: {
        // -F so mkfs does not ask; -m 0 so a USB stick does not reserve 5%
        // of itself for root.
        QStringList arguments = {
            QStringLiteral("-F"), QStringLiteral("-L"), label, QStringLiteral("-m"), QStringLiteral("0")};
        if (const std::optional<DesktopOwner> owner = desktopOwner()) {
            arguments << QStringLiteral("-E") << QStringLiteral("root_owner=%1:%2").arg(owner->uid).arg(owner->gid);
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
                *error = QStringLiteral(
                             "Could not run %1, the tool that formats the drive. Check that it is still "
                             "installed and that the system allows it to run, then try again.")
                             .arg(QFileInfo(tool).fileName());
            }
            return false;
        }
        if (!process.waitForFinished(15 * 60 * 1000)) {
            process.kill();
            process.waitForFinished(5 * 1000);
            if (error) {
                *error = QStringLiteral(
                             "%1 did not finish formatting after 15 minutes and was stopped. The drive may be partly "
                             "formatted: unplug it, plug it back in and run the restore again.")
                             .arg(QFileInfo(tool).fileName());
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
                     ? QStringLiteral(
                           "%1 failed to format the new volume on %2. Unplug the drive, plug it back in "
                           "and run the restore again; if it keeps failing the drive is damaged.")
                           .arg(QFileInfo(tool).fileName(), partitionPath)
                     : QStringLiteral("%1 failed to format the new volume on %2: %3")
                           .arg(QFileInfo(tool).fileName(), partitionPath, lastOutput);
    }
    return false;
}

namespace {

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
            return missing(QStringLiteral(
                "mkfs.exfat was not found. Install exfatprogs (Debian/Ubuntu and Fedora: "
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
            return missing(QStringLiteral(
                "mkfs.ntfs was not found. Install ntfs-3g (Debian/Ubuntu: \"ntfs-3g\", "
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
        // The partition, not the disk, is what mkfs.vfat formats.
        const std::uint64_t length = calculateGptLayout(disk.size, disk.sectorSize).length;
        const std::uint64_t maximum = maximumMkfsFat32VolumeBytes(disk.sectorSize);
        const std::uint64_t minimum = minimumMkfsFat32VolumeBytes(disk.sectorSize);
        if (maximum == 0 || minimum == 0) {
            return refuse(
                QStringLiteral("A disk with %1-byte sectors cannot be formatted as FAT32.").arg(disk.sectorSize));
        }
        if (length > maximum) {
            return refuse(QStringLiteral(
                              "FAT32 cannot address a volume larger than %1 with %2-byte sectors, and "
                              "the rest of the drive would be left unused. Choose exFAT or NTFS.")
                              .arg(QLocale().formattedDataSize(static_cast<qint64>(maximum)))
                              .arg(disk.sectorSize));
        }
        if (length < minimum) {
            return refuse(QStringLiteral(
                              "This drive is too small for a valid FAT32 filesystem; it needs at least "
                              "%1. Choose exFAT.")
                              .arg(QLocale().formattedDataSize(static_cast<qint64>(minimum))));
        }
        return true;
    }
    }
    return refuse(QStringLiteral("That filesystem is not supported."));
}

bool formatAndRereadPartitionTable(
    const RestoreRequest &request,
    const QString &formatTool,
    std::uint64_t partitionStartSector,
    BlockDevice &device,
    LoopDevice &formatter,
    RestoreStepCounter &steps,
    RestoreReporter &reporter,
    std::uint64_t *diskSequence,
    QString *error)
{
    reportPlannedStep(
        steps,
        reporter,
        RestoreStep::Format,
        QStringLiteral("Formatting as %1").arg(fileSystemTypeName(request.fileSystem)));
    if (!formatter.prepareForFormatting(error) ||
        !formatLinuxPartition(
            request.fileSystem,
            formatTool,
            formatter.path(),
            request.volumeLabel,
            partitionStartSector,
            reporter,
            error) ||
        !formatter.detach(error)) {
        formatter.detach(nullptr);
        // The new table is already on the disk; the kernel must not keep the old partitions over it.
        QString rereadError;
        if (!device.flush(&rereadError) || !device.rereadPartitionTable(&rereadError)) {
            reporter.detail(rereadError);
        }
        return false;
    }

    reportPlannedStep(
        steps, reporter, RestoreStep::RereadPartitionTable, QStringLiteral("Re-reading the partition table"));
    if (!device.flush(error) || !device.rereadPartitionTable(error)) {
        return false;
    }
    // The instance the kernel just scanned the new table on; a media change earlier must not fail the restore.
    if (!device.diskSequence(diskSequence)) {
        if (error) {
            *error = QStringLiteral(
                         "The drive %1 was formatted, but it could not be re-identified afterwards. "
                         "Unplug it, plug it back in and check that it shows one new partition.")
                         .arg(request.disk.displayId.isEmpty() ? request.disk.deviceId : request.disk.displayId);
        }
        return false;
    }
    return true;
}

bool performLinuxRestore(
    const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error)
{
    DiskInfo disk = request.disk;
    QString reason;
    if (!isSafeRestoreTarget(disk, request.guard, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }

    // Before anything is overwritten, so a missing mkfs leaves the disk intact.
    const QString formatTool = resolveFormatTool(request.fileSystem, error);
    if (formatTool.isEmpty()) {
        return false;
    }

    RestoreStepCounter steps(linuxRestorePlan());
    reportPlannedStep(steps, reporter, RestoreStep::VerifyDisk, QStringLiteral("Verifying the selected USB disk"));
    DiskInfo currentDisk;
    if (!refreshUsbDisk(disk.deviceId, &currentDisk, error)) {
        return false;
    }
    if (!isSameRestoreTarget(disk, currentDisk, &reason) || !isSafeRestoreTarget(currentDisk, request.guard, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    disk = currentDisk;

    // Repeats the GUI's check on the re-derived disk; a caller could skip the GUI.
    if (!canCreatePartitionLayout(request.style, disk, &reason) ||
        !canCreateFileSystemOn(request.fileSystem, disk, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }

    // Before unmounting, so a wrong device is caught after only a read-only open.
    reportPlannedStep(steps, reporter, RestoreStep::IdentifyDevice, QStringLiteral("Identifying the block device"));
    {
        BlockDevice inspect(disk.deviceId);
        if (!inspect.open(BlockDevice::Access::Inspect, error) || !inspect.verifyIdentity(disk, error)) {
            return false;
        }
    }

    if (reporter.cancelRequested()) {
        return false;
    }

    reportPlannedStep(steps, reporter, RestoreStep::Unmount, QStringLiteral("Unmounting existing filesystems"));
    if (!unmountDisk(disk, reporter, error)) {
        return false;
    }

    // Retried because the desktop automounter can remount the stick between the
    // umount and the open, which O_EXCL then refuses.
    reportPlannedStep(steps, reporter, RestoreStep::OpenExclusive, QStringLiteral("Opening the device exclusively"));
    BlockDevice device(disk.deviceId);
    QString openError;
    bool opened = false;
    for (int attempt = 0; attempt < 3 && !opened; ++attempt) {
        if (attempt > 0) {
            reporter.detail(
                QStringLiteral("The drive is still in use; unmounting it and trying again (attempt %1 of 3)")
                    .arg(attempt + 1));
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

    // Establish the formatter route before erasing; the exclusive loop claim prevents
    // an automounter from mounting an old filesystem exposed by this mapping.
    LoopDevice formatter;
    if (!formatter.configure(
            device.descriptor(), tableRequest.layout.startOffset, tableRequest.layout.length, disk.sectorSize, error)) {
        return false;
    }
    if (reporter.cancelRequested()) {
        return false;
    }
    if (!device.verifyIdentity(disk, error)) {
        return false;
    }

    // No cancellation past here: a half-written table is worse than a finished one.
    reportPlannedStep(
        steps, reporter, RestoreStep::ClearSignatures, QStringLiteral("Clearing old MBR, GPT and ISO signatures"));
    const std::uint64_t sector = disk.sectorSize;
    const std::uint64_t clearBytes = (qMin(SignatureClearBytes, disk.size / 4) / sector) * sector;
    if (clearBytes < sector) {
        if (error) {
            *error =
                QStringLiteral(
                    "%1 reports too little space to clear its old partition signatures. Unplug the drive, plug it back "
                    "in and select it again; if it still reports a tiny size, the drive is damaged.")
                    .arg(disk.displayId.isEmpty() ? disk.deviceId : disk.displayId);
        }
        return false;
    }
    if (!device.writeZeros(0, clearBytes, disk.sectorSize, error) ||
        !device.writeZeros(((disk.size - clearBytes) / sector) * sector, clearBytes, disk.sectorSize, error)) {
        return false;
    }

    reportPlannedStep(
        steps,
        reporter,
        RestoreStep::WritePartitionTable,
        QStringLiteral("Writing the %1 partition table").arg(partitionStyleLabel(request.style)));
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

    if (!device.flush(error)) {
        return false;
    }
    std::uint64_t diskSequence = 0;
    if (!formatAndRereadPartitionTable(
            request,
            formatTool,
            tableRequest.layout.startOffset / disk.sectorSize,
            device,
            formatter,
            steps,
            reporter,
            &diskSequence,
            error)) {
        return false;
    }
    // udev will not create the partition node while the disk is held exclusively.
    device.close();

    reportPlannedStep(
        steps, reporter, RestoreStep::WaitForPartition, QStringLiteral("Waiting for the new partition to appear"));
    const QString diskName = QFileInfo(disk.deviceId).fileName();
    const QString partitionPath = devicePathFor(partitionNameFor(diskName, 1));
    QElapsedTimer timer;
    timer.start();
    qint64 lastWaitNote = 0;
    while (!QFileInfo::exists(partitionPath) && timer.elapsed() < NewPartitionTimeoutMs) {
        QThread::msleep(250);
        // Slow media can take tens of seconds, and silence reads as a stall.
        if (timer.elapsed() - lastWaitNote >= 5000) {
            lastWaitNote = timer.elapsed();
            reporter.detail(QStringLiteral("Still waiting for %1 (%2 s)").arg(partitionPath).arg(lastWaitNote / 1000));
        }
    }
    if (!QFileInfo::exists(partitionPath)) {
        if (error) {
            *error = QStringLiteral(
                         "The new partition did not appear on %1 within %2 seconds. Unplug the drive, "
                         "plug it back in and run the restore again.")
                         .arg(disk.deviceId)
                         .arg(NewPartitionTimeoutMs / 1000);
        }
        return false;
    }

    BlockDevice partition(partitionPath);
    if (!partition.open(BlockDevice::Access::Inspect, error) ||
        !partition.verifyPartitionOf(diskName, diskSequence, error)) {
        return false;
    }

    reportPlannedStep(steps, reporter, RestoreStep::Complete, QStringLiteral("Restore complete"));
    if (result) {
        // Not mounted here: a root mount would leave a volume the user cannot write to.
        result->location = partitionPath;
    }
    return true;
}

} // namespace usbrestore

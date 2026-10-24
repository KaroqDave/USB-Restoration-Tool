#include "win/windows_disk_service.h"

#include "core/safety.h"
#include "win/admin.h"
#include "win/raw_disk.h"
#include "win/volume_manager.h"
#include "win/windows_util.h"

#include <QVector>

#include <memory>

namespace usbrestore {

namespace {

// Slow flash media on a busy machine can take well over ten seconds.
constexpr int NewVolumeTimeoutMs = 60 * 1000;
// Windows removes a volume only after its partition has left the layout.
constexpr int OldVolumeTimeoutMs = 30 * 1000;
constexpr std::uint64_t MaximumWindowsFat32VolumeBytes = 32ull * 1024ull * 1024ull * 1024ull;

quint32 windowsFat32AllocationUnitSize(const DiskInfo &disk)
{
    const GptLayout layout = calculateGptLayout(disk.size, disk.sectorSize);
    return fat32AllocationUnitSize(layout.length, disk.sectorSize, MaximumWindowsFat32VolumeBytes);
}

// After partitioning the stick is blank, so the refusal says how to recover.
bool confirmStillSelected(RawDisk &rawDisk, const DiskInfo &disk, QString *error)
{
    QString reason;
    if (rawDisk.verifyIdentity(disk, &reason)) {
        return true;
    }
    if (error) {
        *error = QStringLiteral("Stopped before formatting disk %1: %2 If the original USB disk was unplugged, its "
                                "partition table has already been rewritten. Plug it back in and run the restore on "
                                "it again.")
                     .arg(disk.number)
                     .arg(reason);
    }
    return false;
}

} // namespace

std::unique_ptr<DiskService> DiskService::create()
{
    return std::make_unique<WindowsDiskService>();
}

PrivilegeMode WindowsDiskService::privilegeMode() const
{
    // No helper on Windows: UAC elevates the whole process at launch or not at all.
    return isProcessElevated() ? PrivilegeMode::Elevated : PrivilegeMode::None;
}

QString WindowsDiskService::privilegeHint() const
{
    return QStringLiteral("USB Restoration Tool needs Administrator permission to restore USB disks.\n\nClose this "
                          "window and start the app again, approving the Windows permission prompt.");
}

QVector<DiskInfo> WindowsDiskService::listUsbDisks(QString *error) const
{
    return m_enumerator.listUsbDisks(error);
}

bool WindowsDiskService::refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error) const
{
    return m_enumerator.diskByNumber(disk.number, current, error);
}

RestoreGuard WindowsDiskService::restoreGuard() const
{
    RestoreGuard guard;
    guard.mountPoints = protectedSystemDriveLetters();
    if (!guard.mountPoints.contains(QStringLiteral("C"))) {
        guard.mountPoints.append(QStringLiteral("C"));
    }
    return guard;
}

QVector<FileSystemType> WindowsDiskService::supportedFileSystems() const
{
    return {FileSystemType::ExFat, FileSystemType::Fat32, FileSystemType::Ntfs};
}

bool WindowsDiskService::canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason) const
{
    const auto refuse = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };

    switch (type) {
    case FileSystemType::ExFat:
    case FileSystemType::Ntfs:
        return true;
    case FileSystemType::Fat32:
        // MSFT_Volume.Format refuses FAT32 above 32 GiB; the partition is smaller than the disk.
        if (calculateGptLayout(disk.size, disk.sectorSize).length > MaximumWindowsFat32VolumeBytes) {
            return refuse(QStringLiteral("Windows cannot format FAT32 on a volume larger than 32 GB. Choose exFAT or "
                                         "NTFS."));
        }
        if (windowsFat32AllocationUnitSize(disk) == 0) {
            return refuse(QStringLiteral("Windows cannot format FAT32 on a volume this small with %1-byte sectors. "
                                         "Choose exFAT or NTFS.")
                              .arg(disk.sectorSize));
        }
        return true;
    case FileSystemType::Ext4:
        return refuse(QStringLiteral("ext4 is not available on Windows."));
    }
    return refuse(QStringLiteral("That filesystem is not supported."));
}

int WindowsDiskService::totalRestoreSteps() const
{
    return static_cast<int>(windowsRestorePlan().size());
}

int WindowsDiskService::firstDestructiveStep() const
{
    return firstDestructiveStepOf(windowsRestorePlan());
}

QString WindowsDiskService::restoredLocationNoun() const
{
    return QStringLiteral("drive letter");
}

bool WindowsDiskService::restore(
    const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error)
{
    DiskInfo disk = request.disk;
    QString reason;
    if (!isSafeRestoreTarget(disk, request.guard, &reason) || !canCreatePartitionLayout(request.style, disk, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }

    VolumeManager volumes;
    RestoreStepCounter steps(windowsRestorePlan());

    reportPlannedStep(steps, reporter, RestoreStep::VerifyDisk, QStringLiteral("Verifying the selected USB disk"));
    DiskInfo currentDisk;
    if (!m_enumerator.diskByNumber(disk.number, &currentDisk, error)) {
        return false;
    }
    if (!isSameRestoreTarget(disk, currentDisk, &reason) || !isSafeRestoreTarget(currentDisk, request.guard, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    disk = currentDisk;

    // Checked against the re-derived disk, not the one handed in.
    if (!canCreatePartitionLayout(request.style, disk, &reason) ||
        !canFormatFileSystem(request.fileSystem, disk, &reason)) {
        if (error) {
            *error = reason;
        }
        return false;
    }
    const quint32 allocationUnitSize =
        request.fileSystem == FileSystemType::Fat32 ? windowsFat32AllocationUnitSize(disk) : 0;

    // Kept open for the whole restore so raw operations hit the verified device,
    // not whatever \\.\PhysicalDriveN names later.
    reportPlannedStep(
        steps, reporter, RestoreStep::OpenDrive, QStringLiteral("Opening and identifying the physical drive"));
    RawDisk rawDisk(disk.number);
    if (!rawDisk.open(error) || !rawDisk.verifyIdentity(disk, error)) {
        return false;
    }

    if (reporter.cancelRequested()) {
        return false;
    }

    reportPlannedStep(
        steps, reporter, RestoreStep::RefreshMetadata, QStringLiteral("Refreshing Windows disk metadata"));
    if (!volumes.refreshDisk(disk.number, error, &reporter)) {
        return false;
    }

    reportPlannedStep(
        steps,
        reporter,
        RestoreStep::DetachDriveLetters,
        QStringLiteral("Detaching existing volumes and drive letters"));
    // Letterless volumes first, so a volume whose letter is about to be removed is not counted twice.
    if (!volumes.dismountUnletteredVolumesOnDisk(disk.number, error, &reporter) ||
        !volumes.removeMountPointsForDisk(disk.number, request.guard.mountPoints, error)) {
        return false;
    }

    // Last harmless stop: deleting the partition records is the first change to the disk itself.
    if (reporter.cancelRequested()) {
        return false;
    }

    // WMI addresses the disk by number, not through the handle, so re-check right before it.
    if (!rawDisk.verifyIdentity(disk, error)) {
        return false;
    }

    reportPlannedStep(
        steps, reporter, RestoreStep::DeletePartitions, QStringLiteral("Deleting existing partition records"));
    bool partitionAtDiskStart = false;
    if (!volumes.deletePartitionsForDisk(disk.number, &partitionAtDiskStart, error, &reporter)) {
        return false;
    }
    if (partitionAtDiskStart) {
        reporter.detail(QStringLiteral("Disk %1 has a partition at its very start that Windows cannot delete, as on "
                                       "a disk with no partition table; the empty table written next replaces it.")
                            .arg(disk.number));
    }

    // An empty GPT written through the verified handle removes what WMI could not, such as a superfloppy's
    // offline volume, which refuses raw writes until Windows takes it away.
    const QStringList oldVolumes = volumes.partitionVolumeDevices(disk.number);
    if (!rawDisk.replaceLayoutWithEmptyGpt(error)) {
        return false;
    }
    if (!volumes.waitForVolumesToLeaveDisk(oldVolumes, disk.number, OldVolumeTimeoutMs, &reporter)) {
        reporter.detail(QStringLiteral("Windows had not removed the old volume on disk %1 after %2 seconds; "
                                       "continuing.")
                            .arg(disk.number)
                            .arg(OldVolumeTimeoutMs / 1000));
    }

    reportPlannedStep(steps, reporter, RestoreStep::LockDrive, QStringLiteral("Locking the physical drive"));
    QString lockError;
    if (!rawDisk.lock(&lockError)) {
        reporter.detail(QStringLiteral("Physical-drive lock skipped: %1").arg(lockError));
    }
    if (!rawDisk.allowExtendedIo(&lockError)) {
        reporter.detail(QStringLiteral("Extended disk I/O hint skipped: %1").arg(lockError));
    }
    rawDisk.refreshLayout(nullptr);

    // Guards the sector writes against the handle having gone stale.
    if (!rawDisk.verifyIdentity(disk, error)) {
        return false;
    }

    reportPlannedStep(
        steps, reporter, RestoreStep::ClearSignatures, QStringLiteral("Clearing old MBR, GPT and ISO signatures"));
    const bool cleared = rawDisk.clearPartitionSignatures(disk.size, disk.sectorSize, error);
    if (rawDisk.notReadyRetries() > 0) {
        reporter.detail(QStringLiteral("Retried the first write to disk %1 %2 time(s) after Windows refused it as not "
                                       "ready while removing the old volume.")
                            .arg(disk.number)
                            .arg(rawDisk.notReadyRetries()));
    }
    if (!cleared) {
        return false;
    }

    reportPlannedStep(steps, reporter, RestoreStep::ResetToRaw, QStringLiteral("Resetting the disk to RAW"));
    if (!rawDisk.setRaw(error)) {
        return false;
    }

    reportPlannedStep(
        steps,
        reporter,
        RestoreStep::CreatePartition,
        QStringLiteral("Creating one %1 data partition").arg(partitionStyleLabel(request.style)));
    if (!rawDisk.createSinglePartition(request.style, request.fileSystem, disk.size, disk.sectorSize, error)) {
        return false;
    }

    reportPlannedStep(
        steps, reporter, RestoreStep::WaitForVolume, QStringLiteral("Waiting for Windows to discover the new volume"));
    const QString volumeName = volumes.waitForVolumeOnDisk(disk.number, NewVolumeTimeoutMs, error, &reporter);
    if (volumeName.isEmpty()) {
        return false;
    }

    reportPlannedStep(
        steps, reporter, RestoreStep::RefreshNewVolume, QStringLiteral("Refreshing Windows disk metadata"));
    if (!volumes.refreshDisk(disk.number, error, &reporter) ||
        !volumes.volumePathBelongsToDisk(volumeName, disk.number, error)) {
        return false;
    }

    // The rest goes by disk number alone; a live handle that still reports it
    // proves no other stick has taken it.
    if (!confirmStillSelected(rawDisk, disk, error)) {
        return false;
    }

    reportPlannedStep(steps, reporter, RestoreStep::AssignDriveLetter, QStringLiteral("Assigning a drive letter"));
    const QString driveRoot = volumes.mountVolume(volumeName, error);
    if (driveRoot.isEmpty()) {
        return false;
    }

    reportPlannedStep(
        steps,
        reporter,
        RestoreStep::Format,
        QStringLiteral("Formatting as %1").arg(fileSystemTypeName(request.fileSystem)));
    if (!confirmStillSelected(rawDisk, disk, error)) {
        return false;
    }
    if (!volumes.formatVolume(
            volumeName,
            driveRoot,
            disk.number,
            request.fileSystem,
            allocationUnitSize,
            request.volumeLabel,
            error,
            &reporter)) {
        return false;
    }

    reportPlannedStep(steps, reporter, RestoreStep::Complete, QStringLiteral("Restore complete"));
    if (result) {
        result->location = driveRoot;
    }
    return true;
}

} // namespace usbrestore

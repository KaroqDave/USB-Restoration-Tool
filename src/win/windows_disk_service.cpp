#include "win/windows_disk_service.h"

#include "core/safety.h"
#include "win/admin.h"
#include "win/raw_disk.h"
#include "win/volume_manager.h"
#include "win/windows_util.h"

#include <QCoreApplication>
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
bool confirmStillSelected(RawDisk &rawDisk, const DiskInfo &disk, QString *error, QString *displayError)
{
    QString reason;
    QString displayReason;
    if (rawDisk.verifyIdentity(disk, &reason, &displayReason)) {
        return true;
    }
    if (error || displayError) {
        setRestoreError(
            error,
            displayError,
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "Stopped before formatting disk %1: %2 If the original USB disk was unplugged, its "
                            "partition table has already been rewritten. Plug it back in and run the restore on "
                            "it again."))
                .arg(disk.number, RestoreMessage{reason, displayReason}));
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
    return QCoreApplication::translate(
        "usbrestore::DiskService",
        "USB Restoration Tool needs Administrator permission to restore USB disks.\n\n"
        "Close this window and start the app again, approving the Windows permission prompt.");
}

QVector<DiskInfo> WindowsDiskService::listUsbDisks(RestoreMessage *error) const
{
    return m_enumerator.listUsbDisks(error);
}

bool WindowsDiskService::refreshDisk(
    const DiskInfo &disk, DiskInfo *current, QString *error, QString *displayError) const
{
    return m_enumerator.diskByNumber(disk.number, current, error, DiskDetails::Display, displayError);
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

bool WindowsDiskService::canFormatFileSystem(
    FileSystemType type, const DiskInfo &disk, QString *reason, QString *displayReason) const
{
    const auto refuse = [reason, displayReason](const RestoreMessage &text) {
        if (reason || displayReason) {
            setRestoreError(reason, displayReason, text);
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
            return refuse(restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "Windows cannot format FAT32 on a volume larger than 32 GB. Choose exFAT or "
                "NTFS.")));
        }
        if (windowsFat32AllocationUnitSize(disk) == 0) {
            return refuse(restoreText(QT_TRANSLATE_NOOP(
                                          "usbrestore::Backend",
                                          "Windows cannot format FAT32 on a volume this small with %1-byte sectors. "
                                          "Choose exFAT or NTFS."))
                              .arg(disk.sectorSize));
        }
        return true;
    case FileSystemType::Ext4:
        return refuse(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "ext4 is not available on Windows.")));
    }
    return refuse(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "That filesystem is not supported.")));
}

const QVector<RestoreStep> &WindowsDiskService::restorePlan() const
{
    return windowsRestorePlan();
}

int WindowsDiskService::totalRestoreSteps() const
{
    return static_cast<int>(windowsRestorePlan().size());
}

int WindowsDiskService::firstDestructiveStep() const
{
    return firstDestructiveStepOf(windowsRestorePlan());
}

bool WindowsDiskService::restore(
    const RestoreRequest &request,
    RestoreReporter &reporter,
    RestoreResult *result,
    QString *error,
    QString *displayError)
{
    DiskInfo disk = request.disk;
    // Emptied at each step, so a failure with only English text cannot show an earlier step's display text.
    RestoreMessage failure;
    const auto stepFailure = [&failure]() {
        return failure.display.isEmpty() ? RestoreMessage(failure.english) : failure;
    };
    const auto refuse = [&stepFailure, error, displayError]() {
        setRestoreError(error, displayError, stepFailure());
        return false;
    };
    const auto failOperation =
        [&disk, &stepFailure, error, displayError](const RestoreMessage &operation, const RestoreMessage &advice = {}) {
            setRestoreError(
                error,
                displayError,
                windowsRestoreErrorMessage(
                    restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not %1 on disk %2"))
                        .arg(operation, disk.number),
                    stepFailure(),
                    advice));
            return false;
        };
    if (!checkRestoreError(
            &failure.english,
            &failure.display,
            [&](QString *text, TextMode mode) { return isSafeRestoreTarget(disk, request.guard, text, mode); }) ||
        !checkRestoreError(&failure.english, &failure.display, [&](QString *text, TextMode mode) {
            return canCreatePartitionLayout(request.style, disk, text, mode);
        })) {
        return refuse();
    }

    RestoreStepCounter steps(windowsRestorePlan());
    const auto beginStep = [&](RestoreStep step) {
        failure = {};
        reportPlannedStep(steps, reporter, step, request);
    };

    beginStep(RestoreStep::VerifyDisk);
    DiskInfo currentDisk;
    if (!m_enumerator.diskByNumber(
            disk.number, &currentDisk, &failure.english, DiskDetails::Restore, &failure.display)) {
        return failOperation(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "verify the selected drive")));
    }
    if (!checkRestoreError(
            &failure.english,
            &failure.display,
            [&](QString *text, TextMode mode) { return isSameRestoreTarget(disk, currentDisk, text, mode); }) ||
        !checkRestoreError(&failure.english, &failure.display, [&](QString *text, TextMode mode) {
            return isSafeRestoreTarget(currentDisk, request.guard, text, mode);
        })) {
        return refuse();
    }
    disk = currentDisk;

    // Checked against the re-derived disk, not the one handed in.
    if (!checkRestoreError(
            &failure.english,
            &failure.display,
            [&](QString *text, TextMode mode) { return canCreatePartitionLayout(request.style, disk, text, mode); }) ||
        !canFormatFileSystem(request.fileSystem, disk, &failure.english, &failure.display)) {
        return refuse();
    }
    const quint32 allocationUnitSize =
        request.fileSystem == FileSystemType::Fat32 ? windowsFat32AllocationUnitSize(disk) : 0;

    // Kept open for the whole restore so raw operations hit the verified device,
    // not whatever \\.\PhysicalDriveN names later.
    beginStep(RestoreStep::OpenDrive);
    RawDisk rawDisk(disk.number);
    if (!rawDisk.open(error, displayError) || !rawDisk.verifyIdentity(disk, error, displayError)) {
        return false;
    }

    if (reporter.cancelRequested()) {
        return false;
    }

    beginStep(RestoreStep::RefreshMetadata);
    VolumeManager volumes;
    if (!volumes.refreshDisk(disk.number, &failure.english, &reporter, &failure.display)) {
        return failOperation(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "refresh Windows disk metadata")));
    }

    beginStep(RestoreStep::DetachDriveLetters);
    // Letterless volumes first, so a volume whose letter is about to be removed is not counted twice.
    if (!volumes.dismountUnletteredVolumesOnDisk(disk.number, &failure.english, &reporter, &failure.display) ||
        !volumes.removeMountPointsForDisk(disk.number, request.guard.mountPoints, &failure.english, &failure.display)) {
        return failOperation(
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "detach the existing volumes and drive letters")),
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "Close any files, File Explorer windows and programs using the drive and run the restore again")));
    }

    // Last harmless stop: deleting the partition records is the first change to the disk itself.
    if (reporter.cancelRequested()) {
        return false;
    }

    // WMI addresses the disk by number, not through the handle, so re-check right before it.
    if (!rawDisk.verifyIdentity(disk, error, displayError)) {
        return false;
    }

    beginStep(RestoreStep::DeletePartitions);
    bool partitionAtDiskStart = false;
    if (!volumes.deletePartitionsForDisk(
            disk.number, &partitionAtDiskStart, &failure.english, &reporter, &failure.display)) {
        return failOperation(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "delete the existing partitions")));
    }
    if (partitionAtDiskStart) {
        reporter.detail(QStringLiteral(
                            "Disk %1 has a partition at its very start that Windows cannot delete, as on "
                            "a disk with no partition table; the empty table written next replaces it.")
                            .arg(disk.number));
    }

    // An empty GPT written through the verified handle removes what WMI could not, such as a superfloppy's
    // offline volume, which refuses raw writes until Windows takes it away.
    const QStringList oldVolumes = volumes.partitionVolumeDevices(disk.number);
    if (!rawDisk.replaceLayoutWithEmptyGpt(error, displayError)) {
        return false;
    }
    if (!volumes.waitForVolumesToLeaveDisk(oldVolumes, disk.number, OldVolumeTimeoutMs, &reporter)) {
        reporter.detail(QStringLiteral(
                            "Windows had not removed the old volume on disk %1 after %2 seconds; "
                            "continuing.")
                            .arg(disk.number)
                            .arg(OldVolumeTimeoutMs / 1000));
    }

    beginStep(RestoreStep::LockDrive);
    QString lockError;
    if (!rawDisk.lock(&lockError)) {
        reporter.detail(QStringLiteral("Physical-drive lock skipped: %1").arg(lockError));
    }
    if (!rawDisk.allowExtendedIo(&lockError)) {
        reporter.detail(QStringLiteral("Extended disk I/O hint skipped: %1").arg(lockError));
    }
    rawDisk.refreshLayout(nullptr);

    // Guards the sector writes against the handle having gone stale.
    if (!rawDisk.verifyIdentity(disk, error, displayError)) {
        return false;
    }

    beginStep(RestoreStep::ClearSignatures);
    const bool cleared = rawDisk.clearPartitionSignatures(disk.size, disk.sectorSize, error, displayError);
    if (rawDisk.notReadyRetries() > 0) {
        reporter.detail(QStringLiteral(
                            "Retried the first write to disk %1 %2 time(s) after Windows refused it as not "
                            "ready while removing the old volume.")
                            .arg(disk.number)
                            .arg(rawDisk.notReadyRetries()));
    }
    if (!cleared) {
        return false;
    }

    beginStep(RestoreStep::ResetToRaw);
    if (!rawDisk.setRaw(error, displayError)) {
        return false;
    }

    beginStep(RestoreStep::CreatePartition);
    if (!rawDisk.createSinglePartition(
            request.style, request.fileSystem, disk.size, disk.sectorSize, error, displayError)) {
        return false;
    }

    beginStep(RestoreStep::WaitForVolume);
    const QString volumeName =
        volumes.waitForVolumeOnDisk(disk.number, NewVolumeTimeoutMs, &failure.english, &reporter, &failure.display);
    if (volumeName.isEmpty()) {
        return failOperation(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "find the new volume")));
    }

    beginStep(RestoreStep::RefreshNewVolume);
    if (!volumes.refreshDisk(disk.number, &failure.english, &reporter, &failure.display) ||
        !volumes.volumePathBelongsToDisk(volumeName, disk.number, &failure.english, &failure.display)) {
        return failOperation(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "verify the new volume")));
    }

    // The rest goes by disk number alone; a live handle that still reports it
    // proves no other stick has taken it.
    if (!confirmStillSelected(rawDisk, disk, error, displayError)) {
        return false;
    }

    beginStep(RestoreStep::AssignDriveLetter);
    const QString driveRoot = volumes.mountVolume(volumeName, &failure.english, &failure.display);
    if (driveRoot.isEmpty()) {
        return failOperation(
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "assign a drive letter")),
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "If every drive letter is in use, eject another drive to free one. Then unplug this drive, plug it "
                "back in and run the restore again")));
    }

    beginStep(RestoreStep::Format);
    if (!confirmStillSelected(rawDisk, disk, error, displayError)) {
        return false;
    }
    if (!volumes.formatVolume(
            volumeName,
            driveRoot,
            disk.number,
            request.fileSystem,
            allocationUnitSize,
            request.volumeLabel,
            &failure.english,
            &reporter,
            &failure.display)) {
        return failOperation(restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "format as %1"))
                                 .arg(fileSystemTypeName(request.fileSystem)));
    }

    beginStep(RestoreStep::Complete);
    if (result) {
        result->location = driveRoot;
    }
    return true;
}

} // namespace usbrestore

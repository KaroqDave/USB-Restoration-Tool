#include "win/restore_worker.h"

#include "core/safety.h"
#include "win/disk_enumerator.h"
#include "win/raw_disk.h"
#include "win/volume_manager.h"

#include <utility>

namespace usbrestore {

namespace {

// How long Windows is given to publish the volume it has just been told to
// create. Slow flash media on a busy machine can take well over ten seconds.
constexpr int NewVolumeTimeoutMs = 60 * 1000;

} // namespace

RestoreWorker::RestoreWorker(DiskInfo disk, QStringList protectedDriveLetters, QString volumeLabel)
    : m_disk(std::move(disk)),
      m_protectedDriveLetters(std::move(protectedDriveLetters)),
      m_volumeLabel(std::move(volumeLabel))
{
}

void RestoreWorker::requestCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool RestoreWorker::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

void RestoreWorker::run()
{
    QString reason;
    if (!isSafeRestoreTarget(m_disk, &reason, m_protectedDriveLetters)) {
        fail(reason);
        return;
    }

    QString error;
    DiskEnumerator enumerator;
    VolumeManager volumes;

    step(QStringLiteral("Verifying the selected USB disk"));
    DiskInfo currentDisk;
    if (!enumerator.diskByNumber(m_disk.number, &currentDisk, &error)) {
        fail(error);
        return;
    }
    if (!isSameRestoreTarget(m_disk, currentDisk, &reason)) {
        fail(reason);
        return;
    }
    if (!isSafeRestoreTarget(currentDisk, &reason, m_protectedDriveLetters)) {
        fail(reason);
        return;
    }
    m_disk = currentDisk;

    // The handle is opened and checked before anything is changed, and it stays
    // open for the rest of the restore. Every raw operation below therefore
    // acts on the device object this check passed on, not on whatever
    // \\.\PhysicalDriveN happens to name later.
    step(QStringLiteral("Opening and identifying the physical drive"));
    RawDisk disk(m_disk.number);
    if (!disk.open(&error)) {
        fail(error);
        return;
    }
    if (!disk.verifyIdentity(m_disk, &error)) {
        fail(error);
        return;
    }

    if (cancelRequested()) {
        emit cancelled();
        return;
    }

    step(QStringLiteral("Refreshing Windows disk metadata"));
    if (!volumes.refreshDisk(m_disk.number, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Detaching existing drive letters"));
    if (!volumes.removeMountPointsForDisk(m_disk.number, m_protectedDriveLetters, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Deleting existing partition records"));
    if (!volumes.deletePartitionsForDisk(m_disk.number, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Locking the physical drive"));
    if (!disk.lock(&error)) {
        emit logMessage(QStringLiteral("Continuing with volume-level locking"));
        emit logFileOnly(QStringLiteral("Physical-drive lock skipped: %1").arg(error));
    }
    if (!disk.allowExtendedIo(&error)) {
        emit logFileOnly(QStringLiteral("Extended disk I/O hint skipped: %1").arg(error));
    }
    disk.refreshLayout(nullptr);

    if (cancelRequested()) {
        emit cancelled();
        return;
    }

    // Last chance to stop. Everything above this line is reversible; nothing
    // below it is, so the identity check is repeated one final time rather
    // than trusting the one from before the WMI calls.
    if (!disk.verifyIdentity(m_disk, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Clearing old MBR, GPT and ISO signatures"));
    if (!disk.clearPartitionSignatures(m_disk.size, m_disk.sectorSize, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Resetting the disk to RAW"));
    if (!disk.setRaw(&error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Creating one GPT data partition"));
    if (!disk.createSingleGptPartition(m_disk.size, m_disk.sectorSize, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Waiting for Windows to discover the new volume"));
    const QString volumeName = volumes.waitForVolumeOnDisk(m_disk.number, NewVolumeTimeoutMs, &error);
    if (volumeName.isEmpty()) {
        fail(error);
        return;
    }

    step(QStringLiteral("Refreshing Windows disk metadata"));
    if (!volumes.refreshDisk(m_disk.number, &error)) {
        fail(error);
        return;
    }
    if (!volumes.volumePathBelongsToDisk(volumeName, m_disk.number, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Assigning a drive letter"));
    const QString driveRoot = volumes.mountVolume(volumeName, &error);
    if (driveRoot.isEmpty()) {
        fail(error);
        return;
    }

    step(QStringLiteral("Formatting as exFAT"));
    if (!volumes.formatExFat(volumeName, driveRoot, m_disk.number, m_volumeLabel, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Restore complete"));
    emit finished(driveRoot);
}

void RestoreWorker::step(const QString &message)
{
    m_step = qMin(m_step + 1, TotalSteps);
    emit logMessage(message);
    emit progress(m_step, TotalSteps, message);
}

void RestoreWorker::fail(const QString &message)
{
    const QString text = message.isEmpty() ? QStringLiteral("The restore failed.") : message;
    emit logMessage(QStringLiteral("ERROR: %1").arg(text));
    emit failed(text);
}

} // namespace usbrestore

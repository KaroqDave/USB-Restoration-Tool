#include "win/RestoreWorker.h"

#include "win/DiskEnumerator.h"
#include "win/RawDisk.h"
#include "win/VolumeManager.h"

#include <QThread>

namespace usbrestore {

RestoreWorker::RestoreWorker(DiskInfo disk)
    : m_disk(std::move(disk))
{
}

void RestoreWorker::run()
{
    QString reason;
    if (!isSafeRestoreTarget(m_disk, &reason)) {
        fail(reason);
        return;
    }

    QString error;
    DiskEnumerator enumerator;
    VolumeManager volumes;

    step(QStringLiteral("Verifying selected USB"));
    DiskInfo currentDisk;
    if (!enumerator.diskByNumber(m_disk.number, &currentDisk, &error)) {
        fail(error);
        return;
    }
    if (!isSameRestoreTarget(m_disk, currentDisk, &reason)) {
        fail(reason);
        return;
    }
    if (!isSafeRestoreTarget(currentDisk, &reason)) {
        fail(reason);
        return;
    }
    m_disk = currentDisk;

    step(QStringLiteral("Refreshing Windows disk metadata"));
    if (!volumes.refreshDisk(m_disk.number, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Detaching existing drive letters"));
    if (!volumes.removeMountPointsForDisk(m_disk.number, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Deleting existing partition records"));
    if (!volumes.deletePartitionsForDisk(m_disk.number, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Opening physical drive"));
    RawDisk disk(m_disk.number);
    if (!disk.open(&error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Locking physical drive"));
    if (!disk.lock(&error)) {
        emit logMessage(QStringLiteral("Using volume-level locking"));
        emit logFileOnly(QStringLiteral("Physical-drive lock skipped: %1").arg(error));
    }

    step(QStringLiteral("Allowing full-disk I/O"));
    if (!disk.allowExtendedIo(&error)) {
        emit logMessage(QStringLiteral("Using standard raw disk I/O"));
        emit logFileOnly(QStringLiteral("Extended disk I/O hint skipped: %1").arg(error));
    }

    step(QStringLiteral("Refreshing disk layout"));
    disk.refreshLayout(nullptr);

    step(QStringLiteral("Clearing old MBR/GPT/ISO signatures"));
    if (!disk.clearPartitionSignatures(m_disk.size, m_disk.sectorSize, &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Resetting disk to RAW"));
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
    QString volumeName;
    for (int attempt = 0; attempt < 60 && volumeName.isEmpty(); ++attempt) {
        QThread::msleep(500);
        volumeName = volumes.findVolumeNameForDisk(m_disk.number, nullptr);
    }
    if (volumeName.isEmpty()) {
        fail(QStringLiteral("Windows did not report a new volume for disk %1.").arg(m_disk.number));
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
    if (!volumes.formatExFat(volumeName, driveRoot, m_disk.number, QStringLiteral("USB"), &error)) {
        fail(error);
        return;
    }

    step(QStringLiteral("Restore complete"));
    emit finished(driveRoot);
}

void RestoreWorker::step(const QString &message)
{
    emit logMessage(message);
    emit progress(message);
}

void RestoreWorker::fail(const QString &message)
{
    const QString text = message.isEmpty() ? QStringLiteral("Restore failed.") : message;
    emit logMessage(QStringLiteral("ERROR: %1").arg(text));
    emit failed(text);
}

}

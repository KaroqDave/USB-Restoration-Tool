#pragma once

#include "core/disk.h"
#include "platform/disk_service.h"

#include <QString>
#include <QStringList>

#include <Windows.h>

namespace usbrestore {

class VolumeManager {
  public:
    // The reporters here get one line per WMI call saying whether Windows queued it as a job.
    bool refreshDisk(quint32 diskNumber, QString *error = nullptr, RestoreReporter *reporter = nullptr) const;

    // Leaves a partition at the start of the disk for the caller's empty partition table to replace,
    // and says through partitionAtDiskStart whether there was one.
    bool deletePartitionsForDisk(
        quint32 diskNumber,
        bool *partitionAtDiskStart,
        QString *error = nullptr,
        RestoreReporter *reporter = nullptr) const;

    // Protected letters (and C:) are never touched, even if Windows claims they live on the target.
    bool
    removeMountPointsForDisk(quint32 diskNumber, const QStringList &protectedLetters, QString *error = nullptr) const;

    // For volumes mounted at a folder or not at all, which the drive-letter pass cannot reach.
    bool dismountUnletteredVolumesOnDisk(
        quint32 diskNumber, QString *error = nullptr, RestoreReporter *reporter = nullptr) const;

    // The volume device behind each partition link, including offline volumes FindFirstVolume skips.
    QStringList partitionVolumeDevices(quint32 diskNumber) const;

    // True once none of the devices is still on the disk; false on timeout.
    bool waitForVolumesToLeaveDisk(
        const QStringList &devices, quint32 diskNumber, int timeoutMs, RestoreReporter *reporter = nullptr) const;

    // Returns the \\?\Volume{...}\ name, empty on timeout. The reporter gets periodic
    // notes because slow media can take most of a minute and silence looks like a stall.
    QString waitForVolumeOnDisk(quint32 diskNumber,
                                int timeoutMs,
                                QString *error = nullptr,
                                RestoreReporter *reporter = nullptr) const;

    QString mountVolume(const QString &volumeName, QString *error = nullptr) const;

    bool volumePathBelongsToDisk(const QString &volumeName, quint32 diskNumber, QString *error = nullptr) const;

    // Re-checks first that the volume still sits on diskNumber.
    bool formatVolume(
        const QString &volumeName,
        const QString &driveRoot,
        quint32 diskNumber,
        FileSystemType fileSystem,
        quint32 allocationUnitSize,
        const QString &label,
        QString *error = nullptr,
        RestoreReporter *reporter = nullptr) const;

  private:
    QString findVolumeNameForDisk(quint32 diskNumber) const;
};

} // namespace usbrestore

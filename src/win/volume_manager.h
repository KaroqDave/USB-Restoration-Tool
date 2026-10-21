#pragma once

#include "core/disk.h"
#include "platform/disk_service.h"

#include <QString>
#include <QStringList>

#include <Windows.h>

namespace usbrestore {

class VolumeManager {
  public:
    bool refreshDisk(quint32 diskNumber, QString *error = nullptr) const;

    // Skips the record Windows invents for a disk with no partition table, which DeleteObject refuses,
    // and reports it through wholeDiskPlaceholder so the caller can reset the layout instead.
    bool deletePartitionsForDisk(quint32 diskNumber, bool *wholeDiskPlaceholder, QString *error = nullptr) const;

    // Protected letters (and C:) are never touched, even if Windows claims they live on the target.
    bool
    removeMountPointsForDisk(quint32 diskNumber, const QStringList &protectedLetters, QString *error = nullptr) const;

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
        QString *error = nullptr) const;

  private:
    QString findVolumeNameForDisk(quint32 diskNumber) const;
};

} // namespace usbrestore

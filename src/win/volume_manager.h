#pragma once

#include "core/disk.h"
#include "platform/disk_service.h"

#include <QString>
#include <QStringList>

#include <Windows.h>

namespace usbrestore {

// The volume-level half of a restore: everything that goes through Windows'
// own storage stack rather than through raw sectors.
class VolumeManager {
  public:
    // Asks Windows to re-read the disk so the storage stack agrees with what
    // is actually on the platter before the next step acts on it.
    bool refreshDisk(quint32 diskNumber, QString *error = nullptr) const;

    bool deletePartitionsForDisk(quint32 diskNumber, QString *error = nullptr) const;

    // Locks, dismounts and unassigns every drive letter that has an extent on
    // this disk. Protected letters are never touched even if Windows claims
    // they live on the target.
    bool
    removeMountPointsForDisk(quint32 diskNumber, const QStringList &protectedLetters, QString *error = nullptr) const;

    // Waits for Windows to publish a volume on the disk, returning its
    // \\?\Volume{...}\ name. An empty result means it never appeared.
    // The reporter, when given, hears a detail line every few seconds of the
    // wait: this loop can legitimately run for most of a minute on slow media,
    // and silence that long reads as a stalled device.
    QString waitForVolumeOnDisk(quint32 diskNumber,
                                int timeoutMs,
                                QString *error = nullptr,
                                RestoreReporter *reporter = nullptr) const;

    QString mountVolume(const QString &volumeName, QString *error = nullptr) const;

    bool volumePathBelongsToDisk(const QString &volumeName, quint32 diskNumber, QString *error = nullptr) const;

    // Formats the volume with the given filesystem and label, having first
    // confirmed once more that the volume still sits on the expected disk.
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

#pragma once

#include <QString>
#include <Windows.h>

namespace usbrestore {

class VolumeManager {
public:
    bool refreshDisk(quint32 diskNumber, QString *error = nullptr) const;
    bool deletePartitionsForDisk(quint32 diskNumber, QString *error = nullptr) const;
    bool removeMountPointsForDisk(quint32 diskNumber, QString *error = nullptr) const;
    QString findVolumeNameForDisk(quint32 diskNumber, QString *error = nullptr) const;
    QString mountVolume(const QString &volumeName, QString *error = nullptr) const;
    bool volumePathBelongsToDisk(const QString &volumeName, quint32 diskNumber, QString *error = nullptr) const;
    bool formatExFat(const QString &volumeName,
                     const QString &driveRoot,
                     quint32 diskNumber,
                     const QString &label,
                     QString *error = nullptr) const;

private:
    bool volumeBelongsToDisk(HANDLE volumeHandle, quint32 diskNumber) const;
};

}

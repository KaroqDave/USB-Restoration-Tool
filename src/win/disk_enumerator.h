#pragma once

#include "core/disk.h"

#include <QVector>

namespace usbrestore {

class WmiObject;

class DiskEnumerator {
  public:
    // An empty list with no error means no USB disk is attached.
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const;

    // Fails if the disk is no longer on the USB bus.
    bool diskByNumber(
        quint32 diskNumber, DiskInfo *disk, QString *error = nullptr, DiskDetails details = DiskDetails::Display) const;

  private:
    DiskInfo diskInfoFromObject(const WmiObject &object, DiskDetails details = DiskDetails::Display) const;
    QStringList driveLettersForDisk(quint32 diskNumber) const;
    void volumeInfoForLetters(const QStringList &letters, QStringList *labels, QStringList *fileSystems) const;
};

} // namespace usbrestore

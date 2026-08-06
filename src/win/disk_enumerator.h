#pragma once

#include "core/disk.h"

#include <QVector>

namespace usbrestore {

class WmiObject;

class DiskEnumerator {
  public:
    // Every USB disk Windows reports, in disk-number order. A failure fills in
    // error and returns an empty list; an empty list with no error simply means
    // no USB disk is attached.
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const;

    // Re-reads one disk by number, refusing anything that is not on the USB
    // bus. Used to revalidate the selection immediately before the destructive
    // work starts.
    bool diskByNumber(quint32 diskNumber, DiskInfo *disk, QString *error = nullptr) const;

  private:
    DiskInfo diskInfoFromObject(const WmiObject &object) const;
    QStringList driveLettersForDisk(quint32 diskNumber) const;
    QStringList labelsForLetters(const QStringList &letters) const;
};

} // namespace usbrestore

#pragma once

#include "win/Core.h"

#include <QVector>

namespace usbrestore {

class WmiObject;

class DiskEnumerator {
public:
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const;
    bool diskByNumber(quint32 diskNumber, DiskInfo *disk, QString *error = nullptr) const;

private:
    DiskInfo diskInfoFromObject(const WmiObject &object) const;
    QStringList driveLettersForDisk(quint32 diskNumber) const;
    QStringList labelsForLetters(const QStringList &letters) const;
};

}

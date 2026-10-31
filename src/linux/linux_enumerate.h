#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace usbrestore {

// Read-only and unprivileged: the facts the helper checks the GUI's claims against.

// "/dev/sdb" for "sdb".
QString devicePathFor(const QString &kernelName);

// Takes a kernel name ("sdb"), not a path, so it cannot be pointed at anything
// that is not a block device the kernel publishes.
DiskInfo diskInfoFor(const QString &diskName, DiskDetails details = DiskDetails::Display);

QVector<DiskInfo> listUsbDiskInfos(QString *error = nullptr);

// Fails for a partition such as "/dev/sdb1", which has no /sys/block directory.
bool refreshUsbDisk(
    const QString &deviceId, DiskInfo *current, QString *error = nullptr, DiskDetails details = DiskDetails::Display);

RestoreGuard linuxRestoreGuard();

// For checking the live mount table right before an unmount, not a snapshot.
const QStringList &protectedSystemMounts();

} // namespace usbrestore

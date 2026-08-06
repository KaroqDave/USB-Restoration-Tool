#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace usbrestore {

// Reading what disks exist and what state they are in, separated from the
// DiskService that presents it. The privileged helper needs exactly this and
// none of the rest of the backend: everything it is told by the GUI is a claim,
// and these are the functions that produce the facts it checks the claim
// against.
//
// All of it is read-only. Nothing here opens a device for writing, and nothing
// here needs to be privileged.

// "/dev/sdb" for "sdb".
QString devicePathFor(const QString &kernelName);

// Everything the tool knows about one whole disk, read fresh from sysfs and
// /proc/self/mountinfo. The caller supplies the kernel name ("sdb"), not a
// path, so there is no way to point this at something that is not a block
// device the kernel is publishing.
DiskInfo diskInfoFor(const QString &diskName);

QVector<DiskInfo> listUsbDiskInfos(QString *error = nullptr);

// Re-reads one disk by device path, refusing a path that no longer names a
// whole disk on the USB bus. "/dev/sdb1" fails here rather than later: a
// partition has no directory under /sys/block.
bool refreshUsbDisk(const QString &deviceId, DiskInfo *current, QString *error = nullptr);

// The filesystems a running Linux system is built out of, plus the disks
// anything is actually mounted on one of them from.
RestoreGuard linuxRestoreGuard();

// The protected mount points on their own, for the check made against the live
// mount table immediately before an unmount rather than against a snapshot.
const QStringList &protectedSystemMounts();

} // namespace usbrestore

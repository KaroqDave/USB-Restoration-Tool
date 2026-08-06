#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QString>
#include <QStringList>
#include <QVector>

#include <memory>

namespace usbrestore {

struct RestoreRequest {
    DiskInfo disk;
    PartitionStyle style = PartitionStyle::Gpt;
    QString volumeLabel;
    RestoreGuard guard;
};

struct RestoreResult {
    // Where the restored volume ended up: "E:\" on Windows, the device node on
    // Linux, where mounting is left to the desktop rather than forced.
    QString location;
};

// How a backend reports progress back to whoever asked for the restore, and
// how it asks whether it should still be running. Implemented by the worker
// that owns the thread, so the backends never touch Qt signals themselves.
class RestoreReporter {
  public:
    virtual ~RestoreReporter() = default;

    // A step the user should see. Backends call this once per stage; the
    // number of stages is whatever totalRestoreSteps() promised.
    virtual void step(const QString &message) = 0;

    // Detail worth having in the log and nowhere else.
    virtual void detail(const QString &message) = 0;

    // Checked at the points where stopping is still harmless. A backend must
    // never abandon a restore once it has begun writing.
    virtual bool cancelRequested() const = 0;
};

// Everything platform-specific behind one interface: what disks exist, what
// must never be touched, and how a restore is actually carried out. The GUI
// and the safety rules are written against this and compile unchanged on
// either platform.
class DiskService {
  public:
    virtual ~DiskService() = default;

    // The backend for the platform this build targets.
    static std::unique_ptr<DiskService> create();

    // Whether a restore can be carried out at all. On Windows that means the
    // process is elevated, because it does the work itself. On Linux it means
    // either the process is root or it can reach the helper that will be, so a
    // false here is a tool that cannot work rather than one that merely was not
    // started with enough privilege.
    virtual bool isPrivileged() const = 0;

    // What to tell the user when it is not, in their platform's terms.
    virtual QString privilegeHint() const = 0;

    virtual QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const = 0;

    // Re-reads one disk, identified the way the platform identifies it.
    virtual bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const = 0;

    // The mount points and devices the running system depends on.
    virtual RestoreGuard restoreGuard() const = 0;

    // How many step() calls a successful restore makes, so progress can be
    // shown as a fraction rather than as a spinner.
    virtual int totalRestoreSteps() const = 0;

    virtual bool restore(const RestoreRequest &request,
                         RestoreReporter &reporter,
                         RestoreResult *result,
                         QString *error) = 0;

    // What the app calls the place a restored volume ends up, for the success
    // message: "drive letter" on Windows, "device" on Linux.
    virtual QString restoredLocationNoun() const = 0;
};

} // namespace usbrestore

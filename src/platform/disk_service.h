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
    FileSystemType fileSystem = FileSystemType::ExFat;
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

// How a restore gets the privilege it needs. The GUI reports this to the
// user; no safety decision may hang off it, because the helper re-derives
// every fact for itself regardless of what this process believes.
enum class PrivilegeMode {
    // This process writes to the disk itself: Windows elevated, or Linux under
    // sudo / the AppImage run as root.
    Elevated,
    // This process stays an ordinary user; a separate root helper does the
    // writing after polkit grants permission for that one restore.
    Helper,
    // Neither route is open. Restores cannot run and the app says so at start.
    None,
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

    // Where the privilege for a restore would come from, if anywhere. On
    // Windows the process either is elevated or nothing works; Linux has the
    // second answer, the installed helper.
    virtual PrivilegeMode privilegeMode() const = 0;

    // Whether a restore can be carried out at all. A false here is a tool
    // that cannot work rather than one that merely was not started with
    // enough privilege.
    bool isPrivileged() const { return privilegeMode() != PrivilegeMode::None; }

    // What to tell the user when it is not, in their platform's terms.
    virtual QString privilegeHint() const = 0;

    virtual QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const = 0;

    // Re-reads one disk, identified the way the platform identifies it.
    virtual bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const = 0;

    // The mount points and devices the running system depends on.
    virtual RestoreGuard restoreGuard() const = 0;

    // Filesystems this backend can format. The GUI fills the Restore combo
    // from this rather than from the OS it was compiled for.
    virtual QVector<FileSystemType> supportedFileSystems() const = 0;

    // Whether a supported filesystem can actually be used with this disk's
    // size and sector geometry. Windows imposes FAT32 limits that Linux does
    // not.
    virtual bool canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason = nullptr) const = 0;

    // How many step() calls a successful restore makes, so progress can be
    // shown as a fraction rather than as a spinner.
    virtual int totalRestoreSteps() const = 0;

    virtual bool
    restore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error) = 0;

    // What the app calls the place a restored volume ends up, for the success
    // message: "drive letter" on Windows, "device" on Linux.
    virtual QString restoredLocationNoun() const = 0;
};

} // namespace usbrestore

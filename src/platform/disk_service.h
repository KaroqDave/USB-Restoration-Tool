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
    // "E:\" on Windows; the device node on Linux, where mounting is left to the desktop.
    QString location;
};

// Implemented by the worker that owns the thread, so backends never touch Qt signals.
class RestoreReporter {
  public:
    virtual ~RestoreReporter() = default;

    // Exactly totalRestoreSteps() calls on success.
    virtual void step(const QString &message) = 0;

    // Log only.
    virtual void detail(const QString &message) = 0;

    // Checked only where stopping is harmless; never abandon a restore once writing began.
    virtual bool cancelRequested() const = 0;
};

// For display only: the helper re-derives every fact, so no safety decision may use it.
enum class PrivilegeMode {
    // Windows elevated, or Linux under sudo / the AppImage run as root.
    Elevated,
    // A root helper writes after polkit grants that one restore.
    Helper,
    None,
};

class DiskService {
  public:
    virtual ~DiskService() = default;

    static std::unique_ptr<DiskService> create();

    virtual PrivilegeMode privilegeMode() const = 0;

    bool isPrivileged() const { return privilegeMode() != PrivilegeMode::None; }

    virtual QString privilegeHint() const = 0;

    virtual QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const = 0;

    virtual bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const = 0;

    virtual RestoreGuard restoreGuard() const = 0;

    // The GUI offers these rather than assuming from the OS it was compiled for.
    virtual QVector<FileSystemType> supportedFileSystems() const = 0;

    // Windows imposes FAT32 size limits that Linux does not.
    virtual bool canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason = nullptr) const = 0;

    virtual int totalRestoreSteps() const = 0;

    // 1-based. The GUI tells the user whether unplugging is still safe from this, so it
    // must never be later than the true first write; move it when steps are added.
    virtual int firstDestructiveStep() const = 0;

    virtual bool
    restore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error) = 0;

    // "drive letter" on Windows, "device" on Linux.
    virtual QString restoredLocationNoun() const = 0;
};

} // namespace usbrestore

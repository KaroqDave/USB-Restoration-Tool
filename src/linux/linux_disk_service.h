#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// A thin shell: enumeration and restore live in linux_enumerate.* and linux_restore.*
// because the privileged helper needs them without this class.
class LinuxDiskService : public DiskService {
  public:
    PrivilegeMode privilegeMode() const override;
    QString privilegeHint() const override;
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const override;
    bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const override;
    RestoreGuard restoreGuard() const override;
    QVector<FileSystemType> supportedFileSystems() const override;
    bool canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason = nullptr) const override;
    const QVector<RestoreStep> &restorePlan() const override;
    int totalRestoreSteps() const override;
    int firstDestructiveStep() const override;
    bool
    restore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error) override;
    QString restoredLocationNoun() const override;
};

} // namespace usbrestore

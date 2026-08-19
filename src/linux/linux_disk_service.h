#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// The Linux backend: sysfs and /proc for what exists and what state it is in,
// a plain file descriptor on /dev/sdX for the raw work, and mkfs for the
// filesystem.
//
// Almost nothing happens here. Enumeration lives in linux_enumerate.* and the
// restore sequence in linux_restore.*, because the privileged helper described
// in docs/polkit-helper.md needs both of those and none of this class. What is
// left is the DiskService interface bolted onto them.
//
// It deliberately does not talk to udisks2. The daemon would add a runtime
// dependency and a polkit round trip without answering any question sysfs
// cannot, and the privilege split this tool wants is one it can make itself.
class LinuxDiskService : public DiskService {
  public:
    PrivilegeMode privilegeMode() const override;
    QString privilegeHint() const override;
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const override;
    bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const override;
    RestoreGuard restoreGuard() const override;
    QVector<FileSystemType> supportedFileSystems() const override;
    bool canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason = nullptr) const override;
    int totalRestoreSteps() const override;
    int firstDestructiveStep() const override;
    bool
    restore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error) override;
    QString restoredLocationNoun() const override;
};

} // namespace usbrestore

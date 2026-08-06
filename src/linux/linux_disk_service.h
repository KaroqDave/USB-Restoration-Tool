#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// The Linux backend: sysfs and /proc for what exists and what state it is in,
// a plain file descriptor on /dev/sdX for the raw work, and mkfs.exfat for the
// filesystem.
//
// It deliberately does not talk to udisks2. The tool already requires root, so
// the daemon would add a runtime dependency and a polkit round trip without
// answering any question sysfs cannot.
class LinuxDiskService : public DiskService {
  public:
    bool isPrivileged() const override;
    QString privilegeHint() const override;
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const override;
    bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const override;
    RestoreGuard restoreGuard() const override;
    int totalRestoreSteps() const override;
    bool restore(const RestoreRequest &request,
                 RestoreReporter &reporter,
                 RestoreResult *result,
                 QString *error) override;
    QString restoredLocationNoun() const override;

  private:
    bool unmountDisk(const DiskInfo &disk, RestoreReporter &reporter, QString *error) const;
    bool formatExFat(const QString &partitionPath, const QString &label, RestoreReporter &reporter,
                     QString *error) const;
};

} // namespace usbrestore

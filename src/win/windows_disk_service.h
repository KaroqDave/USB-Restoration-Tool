#pragma once

#include "platform/disk_service.h"
#include "win/disk_enumerator.h"

namespace usbrestore {

// The Windows backend: MSFT_Storage WMI for what exists and what state it is
// in, DeviceIoControl on \\.\PhysicalDriveN for the raw work.
class WindowsDiskService : public DiskService {
  public:
    PrivilegeMode privilegeMode() const override;
    QString privilegeHint() const override;
    QVector<DiskInfo> listUsbDisks(QString *error = nullptr) const override;
    bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr) const override;
    RestoreGuard restoreGuard() const override;
    QVector<FileSystemType> supportedFileSystems() const override;
    bool canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason = nullptr) const override;
    int totalRestoreSteps() const override;
    bool
    restore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error) override;
    QString restoredLocationNoun() const override;

  private:
    DiskEnumerator m_enumerator;
};

} // namespace usbrestore

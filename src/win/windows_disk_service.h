#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

class WmiObject;

class DiskEnumerator {
  public:
    // An empty list with no error means no USB disk is attached.
    QVector<DiskInfo> listUsbDisks(RestoreMessage *error = nullptr) const;

    // Fails if the disk is no longer on the USB bus.
    bool diskByNumber(
        quint32 diskNumber,
        DiskInfo *disk,
        QString *error = nullptr,
        DiskDetails details = DiskDetails::Display,
        QString *displayError = nullptr) const;

  private:
    DiskInfo diskInfoFromObject(const WmiObject &object, DiskDetails details = DiskDetails::Display) const;
    QStringList driveLettersForDisk(quint32 diskNumber) const;
    void volumeInfoForLetters(const QStringList &letters, QStringList *labels, QStringList *fileSystems) const;
};

// The Windows backend: MSFT_Storage WMI for what exists and what state it is
// in, DeviceIoControl on \\.\PhysicalDriveN for the raw work.
class WindowsDiskService : public DiskService {
  public:
    PrivilegeMode privilegeMode() const override;
    QString privilegeHint() const override;
    QVector<DiskInfo> listUsbDisks(RestoreMessage *error = nullptr) const override;
    bool refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error = nullptr, QString *displayError = nullptr)
        const override;
    RestoreGuard restoreGuard() const override;
    QVector<FileSystemType> supportedFileSystems() const override;
    bool canFormatFileSystem(
        FileSystemType type,
        const DiskInfo &disk,
        QString *reason = nullptr,
        QString *displayReason = nullptr) const override;
    const QVector<RestoreStep> &restorePlan() const override;
    bool restore(
        const RestoreRequest &request,
        RestoreReporter &reporter,
        RestoreResult *result,
        QString *error,
        QString *displayError = nullptr) override;

  private:
    DiskEnumerator m_enumerator;
};

} // namespace usbrestore

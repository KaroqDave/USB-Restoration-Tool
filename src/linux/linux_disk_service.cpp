#include "linux/linux_disk_service.h"

#include "linux/linux_enumerate.h"
#include "linux/linux_restore.h"

#include <QCoreApplication>

#include <unistd.h>

#include <memory>

namespace usbrestore {

std::unique_ptr<DiskService> DiskService::create()
{
    return std::make_unique<LinuxDiskService>();
}

bool LinuxDiskService::isPrivileged() const
{
    return ::geteuid() == 0;
}

QString LinuxDiskService::privilegeHint() const
{
    return QStringLiteral("USB Restoration Tool needs root permission to restore USB disks.\n\nClose this window and "
                          "start it again with sudo, for example:\n\n    sudo %1")
        .arg(QCoreApplication::applicationFilePath());
}

QVector<DiskInfo> LinuxDiskService::listUsbDisks(QString *error) const
{
    return listUsbDiskInfos(error);
}

bool LinuxDiskService::refreshDisk(const DiskInfo &disk, DiskInfo *current, QString *error) const
{
    return refreshUsbDisk(disk.deviceId, current, error);
}

RestoreGuard LinuxDiskService::restoreGuard() const
{
    return linuxRestoreGuard();
}

int LinuxDiskService::totalRestoreSteps() const
{
    return LinuxRestoreStepCount;
}

QString LinuxDiskService::restoredLocationNoun() const
{
    return QStringLiteral("device");
}

bool LinuxDiskService::restore(const RestoreRequest &request,
                               RestoreReporter &reporter,
                               RestoreResult *result,
                               QString *error)
{
    return performLinuxRestore(request, reporter, result, error);
}

} // namespace usbrestore

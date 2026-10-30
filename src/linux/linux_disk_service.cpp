#include "linux/linux_disk_service.h"

#include "linux/helper_client.h"
#include "linux/linux_enumerate.h"
#include "linux/linux_restore.h"

#include <QCoreApplication>
#include <QtGlobal>
#include <QVector>

#include <unistd.h>

#include <memory>

namespace usbrestore {

namespace {

// True under sudo, which the AppImage relies on because it cannot register a polkit action.
bool runningAsRoot()
{
    return ::geteuid() == 0;
}

} // namespace

std::unique_ptr<DiskService> DiskService::create()
{
    return std::make_unique<LinuxDiskService>();
}

PrivilegeMode LinuxDiskService::privilegeMode() const
{
    if (runningAsRoot()) {
        return PrivilegeMode::Elevated;
    }
    return isHelperAvailable() ? PrivilegeMode::Helper : PrivilegeMode::None;
}

QString LinuxDiskService::privilegeHint() const
{
    // Only shown when not root and no helper is installed: an AppImage or build run without sudo.
    return QStringLiteral(
               "USB Restoration Tool cannot get permission to restore USB disks.\n\nInstall it, so that "
               "it can ask for permission when a restore starts, or start it again with sudo:\n\n    "
               "sudo %1")
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

QVector<FileSystemType> LinuxDiskService::supportedFileSystems() const
{
    return {FileSystemType::ExFat, FileSystemType::Fat32, FileSystemType::Ntfs, FileSystemType::Ext4};
}

bool LinuxDiskService::canFormatFileSystem(FileSystemType type, const DiskInfo &disk, QString *reason) const
{
    // Shared so the helper applies exactly this rule to the disk it re-derived.
    return canCreateFileSystemOn(type, disk, reason);
}

const QVector<RestoreStep> &LinuxDiskService::restorePlan() const
{
    return linuxRestorePlan();
}

int LinuxDiskService::totalRestoreSteps() const
{
    return static_cast<int>(linuxRestorePlan().size());
}

int LinuxDiskService::firstDestructiveStep() const
{
    return firstDestructiveStepOf(linuxRestorePlan());
}

bool LinuxDiskService::restore(
    const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error)
{
    if (runningAsRoot()) {
        return performLinuxRestore(request, reporter, result, error);
    }
    return runHelperRestore(request, reporter, result, error);
}

} // namespace usbrestore

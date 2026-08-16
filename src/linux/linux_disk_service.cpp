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

// Whether this process is the one that will do the writing. True under sudo and
// inside the AppImage, which cannot register a polkit action and so keeps the
// old behaviour; false for an ordinary installed run, where the helper does it.
bool runningAsRoot()
{
    return ::geteuid() == 0;
}

} // namespace

std::unique_ptr<DiskService> DiskService::create()
{
    return std::make_unique<LinuxDiskService>();
}

bool LinuxDiskService::isPrivileged() const
{
    // Nothing this process does needs privilege any more except the restore
    // itself, and that is the helper's job. The question is no longer "am I
    // root" but "can a restore be carried out at all", which has two answers.
    return runningAsRoot() || isHelperAvailable();
}

QString LinuxDiskService::privilegeHint() const
{
    // Reached only when both routes are closed: not root, and no installed
    // helper to ask polkit about. That is an AppImage or a build directory
    // started without sudo.
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
    Q_UNUSED(disk);
    switch (type) {
    case FileSystemType::ExFat:
    case FileSystemType::Fat32:
    case FileSystemType::Ntfs:
    case FileSystemType::Ext4:
        return true;
    }
    if (reason) {
        *reason = QStringLiteral("That filesystem is not supported.");
    }
    return false;
}

int LinuxDiskService::totalRestoreSteps() const
{
    return LinuxRestoreStepCount;
}

QString LinuxDiskService::restoredLocationNoun() const
{
    return QStringLiteral("device");
}

bool LinuxDiskService::restore(
    const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error)
{
    // The same restore either way; the only question is which process runs it.
    // Already root means sudo or the AppImage, where there is no privilege to
    // ask for and no policy to ask under.
    if (runningAsRoot()) {
        return performLinuxRestore(request, reporter, result, error);
    }
    return runHelperRestore(request, reporter, result, error);
}

} // namespace usbrestore

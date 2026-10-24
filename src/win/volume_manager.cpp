#include "win/volume_manager.h"

#include "core/safety.h"
#include "win/windows_util.h"
#include "win/wmi.h"

#include <QElapsedTimer>
#include <QThread>
#include <QVector>

#include <winioctl.h>

#include <algorithm>

namespace usbrestore {

namespace {

constexpr auto StorageNamespace = L"ROOT\\Microsoft\\Windows\\Storage";

constexpr int RefreshJobTimeoutMs = 60 * 1000;
constexpr int DeleteJobTimeoutMs = 120 * 1000;
constexpr int FormatJobTimeoutMs = 15 * 60 * 1000;

// From ntddvol.h, which the user-mode SDK headers leave out.
constexpr DWORD IoctlVolumeIsOffline = CTL_CODE(IOCTL_VOLUME_BASE, 4, METHOD_BUFFERED, FILE_ANY_ACCESS);

QString withoutTrailingSlash(QString volumeName)
{
    while (volumeName.endsWith(QLatin1Char('\\'))) {
        volumeName.chop(1);
    }
    return volumeName;
}

QString normalizedVolumePath(QString volumeName)
{
    return volumeName.trimmed().toCaseFolded();
}

QString normalizedLetter(const QString &value)
{
    QString letter = value.trimmed();
    while (letter.endsWith(QLatin1Char('\\'))) {
        letter.chop(1);
    }
    if (letter.endsWith(QLatin1Char(':'))) {
        letter.chop(1);
    }
    return letter.toUpper();
}

QString firstDriveRootFromMultiSz(const wchar_t *paths)
{
    for (const wchar_t *path = paths; path && *path != L'\0'; path += wcslen(path) + 1) {
        const QString current = QString::fromWCharArray(path);
        if (current.size() == 3 && current.at(1) == QLatin1Char(':') && current.endsWith(QLatin1Char('\\'))) {
            return current;
        }
    }
    return {};
}

// Unknown counts as lettered, so the letterless pass only touches a volume that provably has none.
bool mayHaveDriveLetter(const QString &volumeName)
{
    wchar_t paths[512] = {};
    DWORD required = 0;
    if (!GetVolumePathNamesForVolumeNameW(reinterpret_cast<LPCWSTR>(volumeName.utf16()), paths, 512, &required)) {
        return true;
    }
    return !firstDriveRootFromMultiSz(paths).isEmpty();
}

HANDLE openVolumePath(const QString &path, DWORD access)
{
    return CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
        access,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
}

// The handle must already be proven to sit on the target disk.
bool lockAndDismount(HANDLE handle, const QString &volumePath, QString *error)
{
    DWORD unused = 0;
    if (!DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr)) {
        const DWORD code = GetLastError();
        if (error) {
            *error = windowsErrorMessage(
                QStringLiteral("Could not lock volume %1 before restore. Close any window or program using it")
                    .arg(volumePath),
                code);
        }
        return false;
    }
    if (!DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr)) {
        const DWORD code = GetLastError();
        if (error) {
            *error = windowsErrorMessage(
                QStringLiteral("Could not dismount volume %1 before restore").arg(volumePath), code);
        }
        return false;
    }
    return true;
}

} // namespace

namespace {

// Logged success or not: whether Windows queues these as jobs, and how the jobs end, is not documented.
void noteStorageCall(RestoreReporter *reporter, const QString &call, const QString &outcome)
{
    if (reporter && !outcome.isEmpty()) {
        reporter->detail(QStringLiteral("%1 %2").arg(call, outcome));
    }
}

} // namespace

bool VolumeManager::refreshDisk(quint32 diskNumber, QString *error, RestoreReporter *reporter) const
{
    WmiConnection storage(StorageNamespace);
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const auto disks = storage.query(
        QStringLiteral("SELECT * FROM MSFT_Disk WHERE Number = %1 AND BusType = %2").arg(diskNumber).arg(UsbBusType));
    if (disks.isEmpty()) {
        if (error) {
            const QString detail = storage.lastError();
            *error = detail.isEmpty() ? QStringLiteral("Windows did not report USB disk %1.").arg(diskNumber) : detail;
        }
        return false;
    }

    QString outcome;
    const bool refreshed = storage.callMethodAndWait(
        QStringLiteral("MSFT_Disk"), disks.first().objectPath(), L"Refresh", {}, RefreshJobTimeoutMs, error, &outcome);
    noteStorageCall(reporter, QStringLiteral("MSFT_Disk.Refresh"), outcome);
    return refreshed;
}

bool VolumeManager::deletePartitionsForDisk(
    quint32 diskNumber, bool *partitionAtDiskStart, QString *error, RestoreReporter *reporter) const
{
    if (partitionAtDiskStart) {
        *partitionAtDiskStart = false;
    }
    WmiConnection storage(StorageNamespace);
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const auto partitions =
        storage.query(QStringLiteral("SELECT * FROM MSFT_Partition WHERE DiskNumber = %1").arg(diskNumber));
    if (partitions.isEmpty()) {
        if (!storage.lastError().isEmpty()) {
            if (error) {
                *error = storage.lastError();
            }
            return false;
        }
        return true;
    }

    for (const WmiObject &partition : partitions) {
        // DeleteObject refuses the partition Windows invents for a "superfloppy"; the empty partition
        // table the caller writes next replaces it, and an isohybrid entry at LBA 0 too.
        if (isPartitionAtDiskStart(partition.optionalUint64Value(L"Offset"))) {
            if (partitionAtDiskStart) {
                *partitionAtDiskStart = true;
            }
            continue;
        }
        QString outcome;
        const bool deleted = storage.callMethodAndWait(
            QStringLiteral("MSFT_Partition"),
            partition.objectPath(),
            L"DeleteObject",
            {},
            DeleteJobTimeoutMs,
            error,
            &outcome);
        noteStorageCall(reporter, QStringLiteral("MSFT_Partition.DeleteObject"), outcome);
        if (!deleted) {
            return false;
        }
    }

    return true;
}

bool VolumeManager::removeMountPointsForDisk(
    quint32 diskNumber, const QStringList &protectedLetters, QString *error) const
{
    QStringList protectedSet{QStringLiteral("C")};
    for (const QString &letter : protectedLetters) {
        const QString normalized = normalizedLetter(letter);
        if (!normalized.isEmpty() && !protectedSet.contains(normalized)) {
            protectedSet.append(normalized);
        }
    }

    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'D'; letter <= L'Z'; ++letter) {
        const int bit = letter - L'A';
        if ((drives & (1u << bit)) == 0) {
            continue;
        }
        if (protectedSet.contains(normalizedLetter(QString(QChar(letter))))) {
            continue;
        }

        // Query-only first: write access is never requested on a drive not proven to be the target.
        const HANDLE queryHandle = openVolumeForQuery(QChar(letter));
        if (queryHandle == INVALID_HANDLE_VALUE) {
            continue;
        }
        const bool belongs = volumeHasDiskExtent(queryHandle, diskNumber);
        CloseHandle(queryHandle);
        if (!belongs) {
            continue;
        }

        const QString devicePath = QStringLiteral("\\\\.\\%1:").arg(QChar(letter));
        const HANDLE handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(devicePath.utf16()),
            GENERIC_READ | GENERIC_WRITE,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (error) {
                *error = windowsErrorMessage(
                    QStringLiteral("Could not open volume %1 before restore").arg(devicePath), GetLastError());
            }
            return false;
        }

        // The letter could have been reassigned between the two opens.
        if (!volumeHasDiskExtent(handle, diskNumber)) {
            CloseHandle(handle);
            continue;
        }

        if (!lockAndDismount(handle, devicePath, error)) {
            CloseHandle(handle);
            return false;
        }

        const QString mountPoint = QStringLiteral("%1:\\").arg(QChar(letter));
        if (!DeleteVolumeMountPointW(reinterpret_cast<LPCWSTR>(mountPoint.utf16()))) {
            const DWORD code = GetLastError();
            if (code != ERROR_DIR_NOT_EMPTY && code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND &&
                code != ERROR_NOT_A_REPARSE_POINT) {
                if (error) {
                    *error =
                        windowsErrorMessage(QStringLiteral("Could not remove mount point %1").arg(mountPoint), code);
                }
                CloseHandle(handle);
                return false;
            }
        }

        CloseHandle(handle);
    }

    return true;
}

bool VolumeManager::dismountUnletteredVolumesOnDisk(quint32 diskNumber, QString *error, RestoreReporter *reporter) const
{
    wchar_t volumeName[MAX_PATH] = {};
    const HANDLE finder = FindFirstVolumeW(volumeName, MAX_PATH);
    if (finder == INVALID_HANDLE_VALUE) {
        return true;
    }

    bool ok = true;
    do {
        const QString name = QString::fromWCharArray(volumeName);
        if (mayHaveDriveLetter(name)) {
            continue;
        }

        // Query-only first: write access is never requested on a volume not proven to be on the target.
        const QString openName = withoutTrailingSlash(name);
        HANDLE handle = openVolumePath(openName, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        const bool belongs = volumeHasDiskExtent(handle, diskNumber);
        DWORD unused = 0;
        // An offline volume has no filesystem mounted to dismount.
        const bool offline =
            DeviceIoControl(handle, IoctlVolumeIsOffline, nullptr, 0, nullptr, 0, &unused, nullptr) != 0;
        CloseHandle(handle);
        if (!belongs || offline) {
            continue;
        }

        handle = openVolumePath(openName, GENERIC_READ | GENERIC_WRITE);
        if (handle == INVALID_HANDLE_VALUE) {
            const DWORD code = GetLastError();
            if (error) {
                *error = windowsErrorMessage(QStringLiteral("Could not open volume %1 before restore").arg(name), code);
            }
            ok = false;
            break;
        }
        if (!volumeHasDiskExtent(handle, diskNumber)) {
            CloseHandle(handle);
            continue;
        }
        const bool dismounted = lockAndDismount(handle, name, error);
        CloseHandle(handle);
        if (!dismounted) {
            ok = false;
            break;
        }
        if (reporter) {
            reporter->detail(
                QStringLiteral("Dismounted %1, a volume on disk %2 with no drive letter").arg(name).arg(diskNumber));
        }
    } while (FindNextVolumeW(finder, volumeName, MAX_PATH));

    FindVolumeClose(finder);
    return ok;
}

QStringList VolumeManager::partitionVolumeDevices(quint32 diskNumber) const
{
    QStringList devices;
    // Partition numbers can be sparse, so every slot a GPT allows is asked about.
    for (quint32 partition = 1; partition <= GptEntryCount; ++partition) {
        const QString link = QStringLiteral("Harddisk%1Partition%2").arg(diskNumber).arg(partition);
        wchar_t target[MAX_PATH] = {};
        if (QueryDosDeviceW(reinterpret_cast<LPCWSTR>(link.utf16()), target, MAX_PATH) != 0) {
            devices.append(QString::fromWCharArray(target));
        }
    }
    return devices;
}

bool VolumeManager::waitForVolumesToLeaveDisk(
    const QStringList &devices, quint32 diskNumber, int timeoutMs, RestoreReporter *reporter) const
{
    const auto stillOnDisk = [diskNumber](const QString &device) {
        const HANDLE handle = openVolumePath(QStringLiteral("\\\\?\\GLOBALROOT%1").arg(device), 0);
        if (handle == INVALID_HANDLE_VALUE) {
            return false;
        }
        const bool belongs = volumeHasDiskExtent(handle, diskNumber);
        CloseHandle(handle);
        return belongs;
    };

    QElapsedTimer timer;
    timer.start();
    qint64 lastWaitNote = 0;
    while (std::any_of(devices.cbegin(), devices.cend(), stillOnDisk)) {
        if (timer.elapsed() >= timeoutMs) {
            return false;
        }
        if (reporter && timer.elapsed() - lastWaitNote >= 5000) {
            lastWaitNote = timer.elapsed();
            reporter->detail(
                QStringLiteral("Still waiting for Windows to remove the old volume (%1 s)").arg(lastWaitNote / 1000));
        }
        QThread::msleep(250);
    }
    return true;
}

QString VolumeManager::findVolumeNameForDisk(quint32 diskNumber) const
{
    wchar_t volumeName[MAX_PATH] = {};
    const HANDLE finder = FindFirstVolumeW(volumeName, MAX_PATH);
    if (finder == INVALID_HANDLE_VALUE) {
        return {};
    }

    QString result;
    for (;;) {
        const QString current = QString::fromWCharArray(volumeName);
        const QString openName = withoutTrailingSlash(current);
        const HANDLE handle = CreateFileW(
            reinterpret_cast<LPCWSTR>(openName.utf16()),
            0,
            FILE_SHARE_READ | FILE_SHARE_WRITE,
            nullptr,
            OPEN_EXISTING,
            0,
            nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            const bool belongs = volumeHasDiskExtent(handle, diskNumber);
            CloseHandle(handle);
            if (belongs) {
                result = current;
                break;
            }
        }

        if (!FindNextVolumeW(finder, volumeName, MAX_PATH)) {
            break;
        }
    }

    FindVolumeClose(finder);
    return result;
}

QString VolumeManager::waitForVolumeOnDisk(quint32 diskNumber,
                                           int timeoutMs,
                                           QString *error,
                                           RestoreReporter *reporter) const
{
    QElapsedTimer timer;
    timer.start();
    qint64 lastWaitNote = 0;
    for (;;) {
        const QString volumeName = findVolumeNameForDisk(diskNumber);
        if (!volumeName.isEmpty()) {
            return volumeName;
        }
        if (timer.elapsed() >= timeoutMs) {
            break;
        }
        if (reporter && timer.elapsed() - lastWaitNote >= 5000) {
            lastWaitNote = timer.elapsed();
            reporter->detail(QStringLiteral("Still waiting for the new volume (%1 s)").arg(lastWaitNote / 1000));
        }
        QThread::msleep(500);
    }

    if (error) {
        *error = QStringLiteral("Windows did not report a new volume for disk %1 within %2 seconds.")
                     .arg(diskNumber)
                     .arg(timeoutMs / 1000);
    }
    return {};
}

QString VolumeManager::mountVolume(const QString &volumeName, QString *error) const
{
    wchar_t paths[512] = {};
    DWORD required = 0;
    if (GetVolumePathNamesForVolumeNameW(reinterpret_cast<LPCWSTR>(volumeName.utf16()), paths, 512, &required) &&
        paths[0] != L'\0') {
        const QString driveRoot = firstDriveRootFromMultiSz(paths);
        if (!driveRoot.isEmpty()) {
            return driveRoot;
        }
    }

    const QString mountPoint = firstAvailableDriveLetter(GetLogicalDrives());
    if (mountPoint.isEmpty()) {
        if (error) {
            *error = QStringLiteral("No available drive letter was found.");
        }
        return {};
    }

    if (!SetVolumeMountPointW(
            reinterpret_cast<LPCWSTR>(mountPoint.utf16()), reinterpret_cast<LPCWSTR>(volumeName.utf16()))) {
        if (error) {
            *error = windowsErrorMessage(
                QStringLiteral("Could not mount the restored volume at %1").arg(mountPoint), GetLastError());
        }
        return {};
    }

    return mountPoint;
}

bool VolumeManager::volumePathBelongsToDisk(const QString &volumeName, quint32 diskNumber, QString *error) const
{
    const QString openName = withoutTrailingSlash(volumeName);
    const HANDLE handle = CreateFileW(
        reinterpret_cast<LPCWSTR>(openName.utf16()),
        0,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        nullptr,
        OPEN_EXISTING,
        0,
        nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = windowsErrorMessage(QStringLiteral("Could not reopen the restored volume"), GetLastError());
        }
        return false;
    }

    const bool belongs = volumeHasDiskExtent(handle, diskNumber);
    CloseHandle(handle);
    if (!belongs && error) {
        *error = QStringLiteral("The restored volume no longer belongs to disk %1.").arg(diskNumber);
    }
    return belongs;
}

bool VolumeManager::formatVolume(
    const QString &volumeName,
    const QString &driveRoot,
    quint32 diskNumber,
    FileSystemType fileSystem,
    quint32 allocationUnitSize,
    const QString &label,
    QString *error,
    RestoreReporter *reporter) const
{
    const QString formatName = [&]() -> QString {
        switch (fileSystem) {
        case FileSystemType::Fat32:
            return QStringLiteral("FAT32");
        case FileSystemType::Ntfs:
            return QStringLiteral("NTFS");
        case FileSystemType::ExFat:
            return QStringLiteral("exFAT");
        case FileSystemType::Ext4:
            break;
        }
        return {};
    }();
    if (formatName.isEmpty()) {
        if (error) {
            *error = QStringLiteral("Windows cannot format a volume as %1.").arg(fileSystemTypeName(fileSystem));
        }
        return false;
    }
    if (fileSystem == FileSystemType::Fat32 && allocationUnitSize == 0) {
        if (error) {
            *error = QStringLiteral("No valid FAT32 allocation unit was selected before formatting.");
        }
        return false;
    }

    if (!volumePathBelongsToDisk(volumeName, diskNumber, error)) {
        return false;
    }

    WmiConnection storage(StorageNamespace);
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const QString targetPath = normalizedVolumePath(volumeName);
    WmiObject targetVolume;
    QString queryError;
    QElapsedTimer timer;
    timer.start();
    while (!targetVolume.isValid() && timer.elapsed() < 30 * 1000) {
        const auto volumes = storage.query(QStringLiteral("SELECT * FROM MSFT_Volume"));
        if (!storage.lastError().isEmpty()) {
            queryError = storage.lastError();
        }

        for (const WmiObject &volume : volumes) {
            if (normalizedVolumePath(volume.stringValue(L"Path")) == targetPath) {
                targetVolume = volume;
                break;
            }
        }

        if (!targetVolume.isValid()) {
            QThread::msleep(250);
        }
    }

    if (!targetVolume.isValid()) {
        if (error) {
            *error = queryError.isEmpty()
                         ? QStringLiteral("Windows did not report the restored volume for disk %1.").arg(diskNumber)
                         : queryError;
        }
        return false;
    }

    // Labelling afterwards would leave the drive briefly mounted under the old filesystem's name.
    QVector<QPair<QString, QVariant>> inputs = {
        {QStringLiteral("FileSystem"), formatName},
        {QStringLiteral("FileSystemLabel"), label},
        {QStringLiteral("Full"), false},
        {QStringLiteral("Force"), true},
    };
    if (fileSystem == FileSystemType::Fat32) {
        inputs.append({QStringLiteral("AllocationUnitSize"), allocationUnitSize});
    }
    QString outcome;
    const bool formatted = storage.callMethodAndWait(
        QStringLiteral("MSFT_Volume"),
        targetVolume.objectPath(),
        L"Format",
        inputs,
        FormatJobTimeoutMs,
        error,
        &outcome);
    noteStorageCall(reporter, QStringLiteral("MSFT_Volume.Format"), outcome);
    if (!formatted) {
        return false;
    }

    // Trust the filesystem served at the drive root, not the method's return value.
    timer.restart();
    while (timer.elapsed() < 60 * 1000) {
        wchar_t volumeLabel[MAX_PATH + 1] = {};
        wchar_t fileSystemName[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(
                reinterpret_cast<LPCWSTR>(driveRoot.utf16()),
                volumeLabel,
                MAX_PATH,
                nullptr,
                nullptr,
                nullptr,
                fileSystemName,
                MAX_PATH)) {
            if (QString::fromWCharArray(fileSystemName).compare(formatName, Qt::CaseInsensitive) == 0) {
                if (QString::fromWCharArray(volumeLabel).compare(label, Qt::CaseInsensitive) == 0) {
                    return true;
                }
                // Formatted but the label did not take; retry it rather than fail the restore.
                if (SetVolumeLabelW(
                        reinterpret_cast<LPCWSTR>(driveRoot.utf16()), reinterpret_cast<LPCWSTR>(label.utf16()))) {
                    return true;
                }
            }
        }
        QThread::msleep(500);
    }

    if (error) {
        *error = QStringLiteral("The format completed, but Windows did not report a %1 volume labelled %2 at %3.")
                     .arg(formatName, label, driveRoot);
    }
    return false;
}

} // namespace usbrestore

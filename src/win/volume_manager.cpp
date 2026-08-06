#include "win/volume_manager.h"

#include "core/safety.h"
#include "win/windows_util.h"
#include "win/wmi.h"

#include <QElapsedTimer>
#include <QThread>

#include <winioctl.h>

namespace usbrestore {

namespace {

constexpr auto StorageNamespace = L"ROOT\\Microsoft\\Windows\\Storage";

// Storage jobs are queued work; these bound how long the tool waits for one
// before reporting that Windows never finished it.
constexpr int RefreshJobTimeoutMs = 60 * 1000;
constexpr int DeleteJobTimeoutMs = 120 * 1000;
constexpr int FormatJobTimeoutMs = 15 * 60 * 1000;

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

} // namespace

bool VolumeManager::refreshDisk(quint32 diskNumber, QString *error) const
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

    return storage.callMethodAndWait(
        QStringLiteral("MSFT_Disk"), disks.first().objectPath(), L"Refresh", {}, RefreshJobTimeoutMs, error);
}

bool VolumeManager::deletePartitionsForDisk(quint32 diskNumber, QString *error) const
{
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
        if (!storage.callMethodAndWait(QStringLiteral("MSFT_Partition"),
                                       partition.objectPath(),
                                       L"DeleteObject",
                                       {},
                                       DeleteJobTimeoutMs,
                                       error)) {
            return false;
        }
    }

    return true;
}

bool VolumeManager::removeMountPointsForDisk(quint32 diskNumber,
                                             const QStringList &protectedLetters,
                                             QString *error) const
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

        // Ownership is decided on a query-only handle. Write access is only
        // requested for a letter that has already proved to live on the disk
        // being restored, so a bug in the extent check cannot turn into an
        // open write handle on an unrelated drive.
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
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(devicePath.utf16()),
                                          GENERIC_READ | GENERIC_WRITE,
                                          FILE_SHARE_READ | FILE_SHARE_WRITE,
                                          nullptr,
                                          OPEN_EXISTING,
                                          0,
                                          nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            if (error) {
                *error = windowsErrorMessage(QStringLiteral("Could not open volume %1 before restore").arg(devicePath),
                                             GetLastError());
            }
            return false;
        }

        // The extent check is repeated on the writable handle. Between the two
        // opens the letter could in principle have been reassigned, and this
        // handle is the one about to be locked and dismounted.
        if (!volumeHasDiskExtent(handle, diskNumber)) {
            CloseHandle(handle);
            continue;
        }

        DWORD unused = 0;
        bool ok = DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr) != 0;
        if (!ok) {
            if (error) {
                *error = windowsErrorMessage(
                    QStringLiteral("Could not lock volume %1 before restore. Close any window or program using it")
                        .arg(devicePath),
                    GetLastError());
            }
            CloseHandle(handle);
            return false;
        }

        ok = DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr) != 0;
        if (!ok) {
            if (error) {
                *error = windowsErrorMessage(
                    QStringLiteral("Could not dismount volume %1 before restore").arg(devicePath), GetLastError());
            }
            CloseHandle(handle);
            return false;
        }

        const QString mountPoint = QStringLiteral("%1:\\").arg(QChar(letter));
        if (!DeleteVolumeMountPointW(reinterpret_cast<LPCWSTR>(mountPoint.utf16()))) {
            const DWORD code = GetLastError();
            if (code != ERROR_DIR_NOT_EMPTY && code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND &&
                code != ERROR_NOT_A_REPARSE_POINT) {
                if (error) {
                    *error = windowsErrorMessage(QStringLiteral("Could not remove mount point %1").arg(mountPoint),
                                                 code);
                }
                CloseHandle(handle);
                return false;
            }
        }

        CloseHandle(handle);
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
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(openName.utf16()),
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

QString VolumeManager::waitForVolumeOnDisk(quint32 diskNumber, int timeoutMs, QString *error) const
{
    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const QString volumeName = findVolumeNameForDisk(diskNumber);
        if (!volumeName.isEmpty()) {
            return volumeName;
        }
        if (timer.elapsed() >= timeoutMs) {
            break;
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

    if (!SetVolumeMountPointW(reinterpret_cast<LPCWSTR>(mountPoint.utf16()),
                              reinterpret_cast<LPCWSTR>(volumeName.utf16()))) {
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
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(openName.utf16()),
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

bool VolumeManager::formatExFat(const QString &volumeName,
                                const QString &driveRoot,
                                quint32 diskNumber,
                                const QString &label,
                                QString *error) const
{
    // Last check before the format: a volume that has wandered onto another
    // disk is not the one that was just created here.
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

    // The label goes in as a format parameter. Setting it afterwards with
    // SetVolumeLabelW meant a window where the drive was mounted under
    // whatever name the previous filesystem had left behind.
    const QVector<QPair<QString, QVariant>> inputs = {
        {QStringLiteral("FileSystem"), QStringLiteral("exFAT")},
        {QStringLiteral("FileSystemLabel"), label},
        {QStringLiteral("Full"), false},
        {QStringLiteral("Force"), true},
    };
    if (!storage.callMethodAndWait(QStringLiteral("MSFT_Volume"),
                                   targetVolume.objectPath(),
                                   L"Format",
                                   inputs,
                                   FormatJobTimeoutMs,
                                   error)) {
        return false;
    }

    // Confirm through the filesystem, not through the return value: the format
    // is only done when Windows serves an exFAT volume at the drive root.
    timer.restart();
    while (timer.elapsed() < 60 * 1000) {
        wchar_t volumeLabel[MAX_PATH + 1] = {};
        wchar_t fileSystem[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(reinterpret_cast<LPCWSTR>(driveRoot.utf16()),
                                  volumeLabel,
                                  MAX_PATH,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  fileSystem,
                                  MAX_PATH)) {
            if (QString::fromWCharArray(fileSystem).compare(QStringLiteral("exFAT"), Qt::CaseInsensitive) == 0) {
                if (QString::fromWCharArray(volumeLabel).compare(label, Qt::CaseInsensitive) == 0) {
                    return true;
                }
                // Formatted, but the label did not take. Retry it directly
                // rather than failing a restore that has otherwise finished.
                if (SetVolumeLabelW(reinterpret_cast<LPCWSTR>(driveRoot.utf16()),
                                    reinterpret_cast<LPCWSTR>(label.utf16()))) {
                    return true;
                }
            }
        }
        QThread::msleep(500);
    }

    if (error) {
        *error = QStringLiteral("The format completed, but Windows did not report an exFAT volume labelled %1 at %2.")
                     .arg(label, driveRoot);
    }
    return false;
}

} // namespace usbrestore

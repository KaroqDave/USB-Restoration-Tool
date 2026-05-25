#include "win/VolumeManager.h"

#include "win/Core.h"
#include "win/Wmi.h"

#include <QByteArray>
#include <QThread>
#include <Windows.h>
#include <winioctl.h>

namespace usbrestore {

namespace {

QString winErrorMessage(DWORD error = GetLastError())
{
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, error, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    QString message = buffer ? QString::fromWCharArray(buffer).trimmed() : QStringLiteral("Windows error %1").arg(error);
    if (buffer) {
        LocalFree(buffer);
    }
    return message;
}

QString withoutTrailingSlash(QString volumeName)
{
    if (volumeName.endsWith(QLatin1Char('\\'))) {
        volumeName.chop(1);
    }
    return volumeName;
}

QString normalizedVolumePath(QString volumeName)
{
    return volumeName.trimmed().toCaseFolded();
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

bool queryVolumeHasDiskExtent(HANDLE handle, quint32 diskNumber)
{
    DWORD bufferSize = sizeof(VOLUME_DISK_EXTENTS);
    QByteArray buffer(static_cast<int>(bufferSize), '\0');

    for (int attempt = 0; attempt < 5; ++attempt) {
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                                        buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr);
        if (ok) {
            const auto *extents = reinterpret_cast<const VOLUME_DISK_EXTENTS *>(buffer.constData());
            for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) {
                if (extents->Extents[i].DiskNumber == diskNumber) {
                    return true;
                }
            }
            return false;
        }

        if (GetLastError() != ERROR_MORE_DATA) {
            return false;
        }

        bufferSize = qMax<DWORD>(returned, bufferSize + sizeof(DISK_EXTENT) * 16);
        buffer.resize(static_cast<int>(bufferSize));
    }

    return false;
}

}

bool VolumeManager::refreshDisk(quint32 diskNumber, QString *error) const
{
    WmiConnection storage(L"ROOT\\Microsoft\\Windows\\Storage");
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const auto disks = storage.query(QStringLiteral("SELECT * FROM MSFT_Disk WHERE Number = %1").arg(diskNumber));
    if (disks.isEmpty()) {
        if (error) {
            const QString detail = storage.lastError();
            *error = detail.isEmpty()
                ? QStringLiteral("Windows did not report disk %1.").arg(diskNumber)
                : detail;
        }
        return false;
    }

    const quint32 returnValue = storage.callMethod(QStringLiteral("MSFT_Disk"), disks.first().objectPath(), L"Refresh");
    if (returnValue != 0 && returnValue != 4096) {
        if (error) {
            const QString detail = storage.lastError();
            *error = detail.isEmpty()
                ? QStringLiteral("MSFT_Disk.Refresh failed with return value %1.").arg(returnValue)
                : QStringLiteral("MSFT_Disk.Refresh failed with return value %1. %2").arg(returnValue).arg(detail);
        }
        return false;
    }

    return true;
}

bool VolumeManager::deletePartitionsForDisk(quint32 diskNumber, QString *error) const
{
    WmiConnection storage(L"ROOT\\Microsoft\\Windows\\Storage");
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const auto partitions = storage.query(QStringLiteral("SELECT * FROM MSFT_Partition WHERE DiskNumber = %1").arg(diskNumber));
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
        const quint32 returnValue = storage.callMethod(QStringLiteral("MSFT_Partition"), partition.objectPath(), L"DeleteObject");
        if (returnValue != 0 && returnValue != 4096) {
            if (error) {
                const QString detail = storage.lastError();
                *error = detail.isEmpty()
                    ? QStringLiteral("MSFT_Partition.DeleteObject failed with return value %1.").arg(returnValue)
                    : QStringLiteral("MSFT_Partition.DeleteObject failed with return value %1. %2").arg(returnValue).arg(detail);
            }
            return false;
        }
    }

    return true;
}

bool VolumeManager::removeMountPointsForDisk(quint32 diskNumber, QString *error) const
{
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'D'; letter <= L'Z'; ++letter) {
        const int bit = letter - L'A';
        if ((drives & (1u << bit)) == 0) {
            continue;
        }

        const QString path = QStringLiteral("\\\\.\\%1:").arg(QChar(letter));
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        const bool belongs = volumeBelongsToDisk(handle, diskNumber);
        if (belongs) {
            DWORD unused = 0;
            if (!DeviceIoControl(handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr)) {
                if (error) {
                    *error = QStringLiteral("Could not lock volume %1 before restore: %2").arg(path, winErrorMessage());
                }
                CloseHandle(handle);
                return false;
            }
            if (!DeviceIoControl(handle, FSCTL_DISMOUNT_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr)) {
                if (error) {
                    *error = QStringLiteral("Could not dismount volume %1 before restore: %2").arg(path, winErrorMessage());
                }
                CloseHandle(handle);
                return false;
            }
        }

        if (belongs) {
            const QString mountPoint = QStringLiteral("%1:\\").arg(QChar(letter));
            if (!DeleteVolumeMountPointW(reinterpret_cast<LPCWSTR>(mountPoint.utf16()))) {
                const DWORD code = GetLastError();
                if (code != ERROR_DIR_NOT_EMPTY && code != ERROR_FILE_NOT_FOUND && code != ERROR_PATH_NOT_FOUND) {
                    if (error) {
                        *error = QStringLiteral("Could not remove mount point %1: %2").arg(mountPoint, winErrorMessage(code));
                    }
                    CloseHandle(handle);
                    return false;
                }
            }
        }
        CloseHandle(handle);
    }
    return true;
}

QString VolumeManager::findVolumeNameForDisk(quint32 diskNumber, QString *error) const
{
    wchar_t volumeName[MAX_PATH] = {};
    HANDLE finder = FindFirstVolumeW(volumeName, MAX_PATH);
    if (finder == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = winErrorMessage();
        }
        return {};
    }

    QString result;
    for (;;) {
        QString current = QString::fromWCharArray(volumeName);
        const QString openName = withoutTrailingSlash(current);
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(openName.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle != INVALID_HANDLE_VALUE) {
            if (volumeBelongsToDisk(handle, diskNumber)) {
                result = current;
                CloseHandle(handle);
                break;
            }
            CloseHandle(handle);
        }

        if (!FindNextVolumeW(finder, volumeName, MAX_PATH)) {
            break;
        }
    }

    FindVolumeClose(finder);
    return result;
}

QString VolumeManager::mountVolume(const QString &volumeName, QString *error) const
{
    wchar_t paths[512] = {};
    DWORD required = 0;
    if (GetVolumePathNamesForVolumeNameW(reinterpret_cast<LPCWSTR>(volumeName.utf16()), paths, 512, &required) && paths[0] != L'\0') {
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

    if (!SetVolumeMountPointW(reinterpret_cast<LPCWSTR>(mountPoint.utf16()), reinterpret_cast<LPCWSTR>(volumeName.utf16()))) {
        if (error) {
            *error = QStringLiteral("Could not mount volume %1 at %2: %3").arg(volumeName, mountPoint, winErrorMessage());
        }
        return {};
    }

    return mountPoint;
}

bool VolumeManager::volumePathBelongsToDisk(const QString &volumeName, quint32 diskNumber, QString *error) const
{
    const QString openName = withoutTrailingSlash(volumeName);
    HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(openName.utf16()), GENERIC_READ,
                                FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = QStringLiteral("Could not reopen restored volume %1: %2").arg(volumeName, winErrorMessage());
        }
        return false;
    }

    const bool belongs = volumeBelongsToDisk(handle, diskNumber);
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
    if (!volumePathBelongsToDisk(volumeName, diskNumber, error)) {
        return false;
    }

    WmiConnection storage(L"ROOT\\Microsoft\\Windows\\Storage");
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const QString targetPath = normalizedVolumePath(volumeName);
    WmiObject targetVolume;
    bool foundVolume = false;
    QString queryError;
    for (int attempt = 0; attempt < 40 && !foundVolume; ++attempt) {
        const auto volumes = storage.query(QStringLiteral("SELECT * FROM MSFT_Volume"));
        if (!storage.lastError().isEmpty()) {
            queryError = storage.lastError();
        }

        for (const WmiObject &volume : volumes) {
            if (normalizedVolumePath(volume.stringValue(L"Path")) == targetPath) {
                targetVolume = volume;
                foundVolume = true;
                break;
            }
        }

        if (!foundVolume) {
            QThread::msleep(250);
        }
    }

    if (!foundVolume) {
        if (error) {
            *error = queryError.isEmpty()
                ? QStringLiteral("Windows did not report the restored volume %1.").arg(volumeName)
                : queryError;
        }
        return false;
    }

    const QVector<QPair<QString, QVariant>> inputs = {
        {QStringLiteral("FileSystem"), QStringLiteral("exFAT")},
        {QStringLiteral("Full"), false}
    };
    const quint32 returnValue = storage.callMethod(QStringLiteral("MSFT_Volume"), targetVolume.objectPath(), L"Format", inputs);
    if (returnValue != 0 && returnValue != 4096) {
        if (error) {
            const QString detail = storage.lastError();
            *error = detail.isEmpty()
                ? QStringLiteral("MSFT_Volume.Format failed with return value %1.").arg(returnValue)
                : QStringLiteral("MSFT_Volume.Format failed with return value %1. %2").arg(returnValue).arg(detail);
        }
        return false;
    }

    for (int attempt = 0; attempt < 120; ++attempt) {
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
                for (int labelAttempt = 0; labelAttempt < 20; ++labelAttempt) {
                    if (SetVolumeLabelW(reinterpret_cast<LPCWSTR>(driveRoot.utf16()), reinterpret_cast<LPCWSTR>(label.utf16()))) {
                        return true;
                    }
                    QThread::msleep(250);
                }

                if (error) {
                    *error = QStringLiteral("The volume was formatted as exFAT, but setting the label failed: %1").arg(winErrorMessage());
                }
                return false;
            }
        }
        QThread::msleep(500);
    }

    if (error) {
        *error = QStringLiteral("The format command completed, but Windows did not report an exFAT volume at %1.").arg(driveRoot);
    }
    return false;
}

bool VolumeManager::volumeBelongsToDisk(HANDLE volumeHandle, quint32 diskNumber) const
{
    return queryVolumeHasDiskExtent(volumeHandle, diskNumber);
}

}

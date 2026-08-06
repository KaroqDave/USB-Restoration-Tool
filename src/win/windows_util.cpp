#include "win/windows_util.h"

#include <QByteArray>
#include <QtGlobal>

#include <shellapi.h>
#include <winioctl.h>

#include <cstddef>

namespace usbrestore {

QString windowsErrorMessage(DWORD error)
{
    wchar_t *buffer = nullptr;
    const DWORD length = FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM |
                                            FORMAT_MESSAGE_IGNORE_INSERTS,
                                        nullptr,
                                        error,
                                        0,
                                        reinterpret_cast<LPWSTR>(&buffer),
                                        0,
                                        nullptr);
    QString message;
    if (length > 0 && buffer) {
        message = QString::fromWCharArray(buffer, static_cast<int>(length)).trimmed();
    }
    if (buffer) {
        LocalFree(buffer);
    }
    if (message.isEmpty()) {
        return QStringLiteral("Windows error %1").arg(error);
    }
    return QStringLiteral("%1 (error %2)").arg(message).arg(error);
}

QString windowsErrorMessage()
{
    return windowsErrorMessage(GetLastError());
}

QString windowsErrorMessage(const QString &context, DWORD error)
{
    return QStringLiteral("%1: %2").arg(context, windowsErrorMessage(error));
}

bool volumeHasDiskExtent(HANDLE volumeHandle, quint32 diskNumber)
{
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    QByteArray buffer(static_cast<int>(sizeof(VOLUME_DISK_EXTENTS)), '\0');
    for (int attempt = 0; attempt < 5; ++attempt) {
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(volumeHandle,
                                        IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS,
                                        nullptr,
                                        0,
                                        buffer.data(),
                                        static_cast<DWORD>(buffer.size()),
                                        &returned,
                                        nullptr);
        if (ok) {
            constexpr DWORD headerSize = offsetof(VOLUME_DISK_EXTENTS, Extents);
            if (returned < headerSize) {
                return false;
            }

            const auto *extents = reinterpret_cast<const VOLUME_DISK_EXTENTS *>(buffer.constData());
            // NumberOfDiskExtents is a count reported by the driver. It is
            // trusted only as far as the bytes that actually came back, so a
            // wrong count cannot walk this loop off the end of the buffer.
            const DWORD fits = (returned - headerSize) / sizeof(DISK_EXTENT);
            const DWORD count = qMin(extents->NumberOfDiskExtents, fits);
            for (DWORD i = 0; i < count; ++i) {
                if (extents->Extents[i].DiskNumber == diskNumber) {
                    return true;
                }
            }
            return false;
        }

        const DWORD code = GetLastError();
        if (code != ERROR_MORE_DATA && code != ERROR_INSUFFICIENT_BUFFER) {
            return false;
        }
        buffer.resize(buffer.size() + static_cast<int>(sizeof(DISK_EXTENT)) * 16);
    }

    return false;
}

HANDLE openVolumeForQuery(QChar driveLetter)
{
    const QString path = QStringLiteral("\\\\.\\%1:").arg(driveLetter);
    return CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                       0,
                       FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr,
                       OPEN_EXISTING,
                       0,
                       nullptr);
}

QStringList protectedSystemDriveLetters()
{
    QStringList letters;

    const auto append = [&letters](const QString &path) {
        if (path.size() >= 2 && path.at(1) == QLatin1Char(':')) {
            const QString letter = path.left(1).toUpper();
            if (!letters.contains(letter)) {
                letters.append(letter);
            }
        }
    };

    wchar_t windowsDirectory[MAX_PATH] = {};
    if (GetWindowsDirectoryW(windowsDirectory, MAX_PATH) > 0) {
        append(QString::fromWCharArray(windowsDirectory));
    }

    wchar_t systemDirectory[MAX_PATH] = {};
    if (GetSystemDirectoryW(systemDirectory, MAX_PATH) > 0) {
        append(QString::fromWCharArray(systemDirectory));
    }

    wchar_t modulePath[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, modulePath, MAX_PATH) > 0) {
        append(QString::fromWCharArray(modulePath));
    }

    return letters;
}

bool openAsInvokingUser(const QString &target)
{
    // The target is quoted for explorer's command line, so a quote inside it
    // would end the argument early. Nothing this app opens contains one.
    if (target.isEmpty() || target.contains(QLatin1Char('"'))) {
        return false;
    }

    const QString parameters = QStringLiteral("\"%1\"").arg(target);

    SHELLEXECUTEINFOW info = {};
    info.cbSize = sizeof(info);
    info.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
    info.lpVerb = L"open";
    info.lpFile = L"explorer.exe";
    info.lpParameters = reinterpret_cast<LPCWSTR>(parameters.utf16());
    info.nShow = SW_SHOWNORMAL;

    return ShellExecuteExW(&info) != FALSE;
}

} // namespace usbrestore

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
    const DWORD length = FormatMessageW(
        FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
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

QString windowsDeviceErrorText(const QString &context, DWORD error)
{
    QString reason;
    QString advice = QStringLiteral("Unplug the drive, plug it back in and try again");
    switch (error) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY:
        reason = QStringLiteral("another program is still using the drive");
        advice = QStringLiteral("Close any files, File Explorer windows and programs using the drive and try again");
        break;
    case ERROR_WRITE_PROTECT:
        reason = QStringLiteral("the drive is write-protected");
        advice = QStringLiteral(
            "If it has a write-protect switch, turn it off and try again. If it has none, the drive may have made "
            "itself read-only and need replacing");
        break;
    case ERROR_ACCESS_DENIED:
        reason = QStringLiteral("Windows refused access to the drive");
        advice = QStringLiteral(
            "Close any programs using the drive. If it has a write-protect switch, turn it off and try again. "
            "If it stays read-only, the drive may have made itself read-only and need replacing");
        break;
    case ERROR_IO_DEVICE:
    case ERROR_CRC:
    case ERROR_READ_FAULT:
    case ERROR_WRITE_FAULT:
        reason = QStringLiteral("the drive could not complete the read or write");
        advice = QStringLiteral(
            "Unplug it, plug it back in and try again; if it keeps happening, try another USB port or replace the "
            "drive");
        break;
    case ERROR_NOT_READY:
        reason = QStringLiteral("the drive is not ready");
        break;
    case ERROR_DEV_NOT_EXIST:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_NO_MEDIA_IN_DRIVE:
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        reason = QStringLiteral("the drive is no longer available");
        advice = QStringLiteral("Plug it back in, refresh the list and select it again");
        break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        reason = QStringLiteral("the drive ran out of room before its reported size");
        advice = QStringLiteral(
            "Unplug it, plug it back in and try again; if it happens again the drive may be smaller than it claims");
        break;
    default:
        return QStringLiteral("%1: %2. %3.").arg(context, windowsErrorMessage(error), advice);
    }
    return QStringLiteral("%1: %2 (Windows error %3). %4.").arg(context, reason).arg(error).arg(advice);
}

QString windowsRestoreErrorText(const QString &context, const QString &detail, const QString &advice)
{
    const QString nextStep =
        advice.isEmpty()
            ? QStringLiteral("Close any programs using the drive, unplug it, plug it back in and run the restore again")
            : advice;
    QString message = QStringLiteral("%1. %2.").arg(context, nextStep);
    if (!detail.isEmpty()) {
        message += QStringLiteral("\n\nDetails: %1").arg(detail);
    }
    return message;
}

QString firstAvailableDriveLetter(quint32 logicalDrivesMask)
{
    for (int code = 'D'; code <= 'Z'; ++code) {
        const int bit = code - 'A';
        if ((logicalDrivesMask & (1u << bit)) == 0) {
            return QString(QChar(static_cast<ushort>(code))) + QStringLiteral(":\\");
        }
    }
    return {};
}

bool volumeHasDiskExtent(HANDLE volumeHandle, quint32 diskNumber)
{
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return false;
    }

    QByteArray buffer(static_cast<int>(sizeof(VOLUME_DISK_EXTENTS)), '\0');
    for (int attempt = 0; attempt < 5; ++attempt) {
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(
            volumeHandle,
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
            // The driver's count is bounded by the bytes returned so it cannot overrun the buffer.
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
    return CreateFileW(
        reinterpret_cast<LPCWSTR>(path.utf16()),
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
    // A quote would end explorer's quoted argument early.
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

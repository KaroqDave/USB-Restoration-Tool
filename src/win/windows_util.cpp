#include "win/windows_util.h"

#include "core/safety.h"
#include "platform/startup.h"

#include <QByteArray>
#include <QVector>
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

RestoreMessage windowsDeviceErrorMessage(const RestoreMessage &context, DWORD error)
{
    RestoreMessage reason;
    RestoreMessage advice =
        restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Unplug the drive, plug it back in and try again"));
    switch (error) {
    case ERROR_SHARING_VIOLATION:
    case ERROR_LOCK_VIOLATION:
    case ERROR_BUSY:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "another program is still using the drive"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Close any files, File Explorer windows and programs using the drive and try again"));
        break;
    case ERROR_WRITE_PROTECT:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive is write-protected"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "If it has a write-protect switch, turn it off and try again. If it has none, the drive may have made "
            "itself read-only and need replacing"));
        break;
    case ERROR_ACCESS_DENIED:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Windows refused access to the drive"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Close any programs using the drive. If it has a write-protect switch, turn it off and try again. "
            "If it stays read-only, the drive may have made itself read-only and need replacing"));
        break;
    case ERROR_IO_DEVICE:
    case ERROR_CRC:
    case ERROR_READ_FAULT:
    case ERROR_WRITE_FAULT:
        reason =
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive could not complete the read or write"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Unplug it, plug it back in and try again; if it keeps happening, try another USB port or replace the "
            "drive"));
        break;
    case ERROR_NOT_READY:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive is not ready"));
        break;
    case ERROR_DEV_NOT_EXIST:
    case ERROR_DEVICE_NOT_CONNECTED:
    case ERROR_NO_MEDIA_IN_DRIVE:
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive is no longer available"));
        advice = restoreText(
            QT_TRANSLATE_NOOP("usbrestore::Backend", "Plug it back in, refresh the list and select it again"));
        break;
    case ERROR_DISK_FULL:
    case ERROR_HANDLE_DISK_FULL:
        reason =
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive ran out of room before its reported size"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Unplug it, plug it back in and try again; if it happens again the drive may be smaller than it claims"));
        break;
    default:
        return RestoreMessage{QStringLiteral("%1: %2. %3.")}.arg(context, windowsErrorMessage(error), advice);
    }
    return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1: %2 (Windows error %3). %4."))
        .arg(context, reason, error, advice);
}

RestoreMessage
windowsRestoreErrorMessage(const RestoreMessage &context, const RestoreMessage &detail, const RestoreMessage &advice)
{
    const RestoreMessage nextStep =
        advice.english.isEmpty()
            ? restoreText(QT_TRANSLATE_NOOP(
                  "usbrestore::Backend",
                  "Close any programs using the drive, unplug it, plug it back in and run the restore again"))
            : advice;
    if (detail.english.isEmpty()) {
        return RestoreMessage{QStringLiteral("%1. %2.")}.arg(context, nextStep);
    }
    return RestoreMessage{QStringLiteral("%1. %2.%3")}.arg(
        context, nextStep, restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "\n\nDetails: %1")).arg(detail));
}

RestoreMessage windowsErrorDetails(const RestoreMessage &context, DWORD error)
{
    return RestoreMessage{QStringLiteral("%1: %2"), QStringLiteral("%1: %2")}.arg(context, windowsErrorMessage(error));
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

namespace {

QVector<quint32> volumeDiskNumbers(HANDLE volumeHandle)
{
    QVector<quint32> numbers;
    if (volumeHandle == INVALID_HANDLE_VALUE) {
        return numbers;
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
            const auto *extents = reinterpret_cast<const VOLUME_DISK_EXTENTS *>(buffer.constData());
            const std::uint32_t count = boundedEntryCount(
                extents->NumberOfDiskExtents, returned, offsetof(VOLUME_DISK_EXTENTS, Extents), sizeof(DISK_EXTENT));
            for (std::uint32_t i = 0; i < count; ++i) {
                numbers.append(extents->Extents[i].DiskNumber);
            }
            return numbers;
        }

        const DWORD code = GetLastError();
        if (code != ERROR_MORE_DATA && code != ERROR_INSUFFICIENT_BUFFER) {
            return numbers;
        }
        buffer.resize(buffer.size() + static_cast<int>(sizeof(DISK_EXTENT)) * 16);
    }

    return numbers;
}

} // namespace

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

bool volumeHasDiskExtent(HANDLE volumeHandle, quint32 diskNumber)
{
    return volumeDiskNumbers(volumeHandle).contains(diskNumber);
}

bool volumePathIsOnDisk(const QString &path, quint32 diskNumber, DWORD *openError)
{
    const HANDLE handle = openVolumePath(path, 0);
    if (handle == INVALID_HANDLE_VALUE) {
        if (openError) {
            const DWORD code = GetLastError();
            *openError = code == ERROR_SUCCESS ? ERROR_OPEN_FAILED : code;
        }
        return false;
    }
    if (openError) {
        *openError = ERROR_SUCCESS;
    }
    const bool belongs = volumeHasDiskExtent(handle, diskNumber);
    CloseHandle(handle);
    return belongs;
}

QHash<quint32, QStringList> driveLettersByDisk()
{
    QHash<quint32, QStringList> letters;
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        if ((drives & (1u << (letter - L'A'))) == 0) {
            continue;
        }
        const QString name = QStringLiteral("%1:").arg(QChar(letter));
        const HANDLE handle = openVolumePath(QStringLiteral("\\\\.\\") + name, 0);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        const QVector<quint32> disks = volumeDiskNumbers(handle);
        CloseHandle(handle);
        for (const quint32 disk : disks) {
            if (!letters[disk].contains(name)) {
                letters[disk].append(name);
            }
        }
    }
    return letters;
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

bool isProcessElevated()
{
    BOOL isAdmin = FALSE;
    PSID adminGroup = nullptr;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
    if (AllocateAndInitializeSid(
            &ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        if (!CheckTokenMembership(nullptr, adminGroup, &isAdmin)) {
            isAdmin = FALSE;
        }
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

void hardenProcessStartup()
{
    // Excludes the current directory and %PATH%, the easiest for an unprivileged user to plant in.
    if (!SetDefaultDllDirectories(
            LOAD_LIBRARY_SEARCH_SYSTEM32 | LOAD_LIBRARY_SEARCH_APPLICATION_DIR | LOAD_LIBRARY_SEARCH_USER_DIRS)) {
        // Fallback for older systems: at least drops the current directory.
        SetDllDirectoryW(L"");
    }
}

} // namespace usbrestore

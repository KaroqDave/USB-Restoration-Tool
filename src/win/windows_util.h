#pragma once

#include "core/restore_message.h"

#include <QHash>
#include <QString>
#include <QStringList>

#include <Windows.h>

namespace usbrestore {

QString windowsErrorMessage(DWORD error);

RestoreMessage windowsDeviceErrorMessage(const RestoreMessage &context, DWORD error);
RestoreMessage windowsRestoreErrorMessage(
    const RestoreMessage &context, const RestoreMessage &detail, const RestoreMessage &advice = {});
RestoreMessage windowsErrorDetails(const RestoreMessage &context, DWORD error);

// No access is query-only, so a volume the tool does not own is never opened writable.
HANDLE openVolumePath(const QString &path, DWORD access);

// The only test of whether a volume belongs to the target, asked through the handle
// rather than inferred from any name.
bool volumeHasDiskExtent(HANDLE volumeHandle, quint32 diskNumber);

// Opens the path query-only, asks volumeHasDiskExtent() and closes it. When the open fails,
// *openError holds why; otherwise it is ERROR_SUCCESS.
bool volumePathIsOnDisk(const QString &path, quint32 diskNumber, DWORD *openError = nullptr);

// Every present drive letter ("E:"), keyed by each disk its volume has an extent on,
// from one query-only open per letter.
QHash<quint32, QStringList> driveLettersByDisk();

// Starts at D:, leaving A:/B: to floppy-era naming. Empty when none is free.
QString firstAvailableDriveLetter(quint32 logicalDrivesMask);

// The Windows and executable drives; C: is added by the core safety check itself.
QStringList protectedSystemDriveLetters();

// Routes through explorer.exe so a handler such as a browser does not inherit this
// process's elevation. False if the target contains a quote.
bool openAsInvokingUser(const QString &target);

// The manifest requests elevation, so false means stop now rather than fail mid-operation.
bool isProcessElevated();

} // namespace usbrestore

#pragma once

#include <QString>
#include <QStringList>

#include <Windows.h>

namespace usbrestore {

QString windowsErrorMessage(DWORD error);
QString windowsErrorMessage();
QString windowsErrorMessage(const QString &context, DWORD error);

QString windowsDeviceErrorText(const QString &context, DWORD error);
QString windowsRestoreErrorText(const QString &context, const QString &detail);

// The only test of whether a drive letter belongs to the target, asked through the handle
// rather than inferred from any name.
bool volumeHasDiskExtent(HANDLE volumeHandle, quint32 diskNumber);

// Starts at D:, leaving A:/B: to floppy-era naming. Empty when none is free.
QString firstAvailableDriveLetter(quint32 logicalDrivesMask);

// No read or write access, so a drive the tool does not own is never opened writable.
HANDLE openVolumeForQuery(QChar driveLetter);

// The Windows and executable drives; C: is added by the core safety check itself.
QStringList protectedSystemDriveLetters();

// Routes through explorer.exe so a handler such as a browser does not inherit this
// process's elevation. False if the target contains a quote.
bool openAsInvokingUser(const QString &target);

} // namespace usbrestore

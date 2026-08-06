#pragma once

#include <QString>
#include <QStringList>

#include <Windows.h>

namespace usbrestore {

// The text Windows gives for an error code, with the code appended so a report
// stays actionable even when the message is generic.
QString windowsErrorMessage(DWORD error);
QString windowsErrorMessage();
QString windowsErrorMessage(const QString &context, DWORD error);

// Whether the volume behind an open handle has an extent on the given disk.
// This is the only question that decides whether a drive letter belongs to the
// disk about to be erased, so it is asked through the handle rather than
// inferred from any name.
bool volumeHasDiskExtent(HANDLE volumeHandle, quint32 diskNumber);

// The first free drive letter at or after D:. C: is never offered, and A:/B:
// are left to floppy-era device naming. Windows-only by nature: Linux has no
// equivalent question.
QString firstAvailableDriveLetter(quint32 logicalDrivesMask);

// Opens \\.\X: for querying only. No read or write access is requested,
// because everything this tool asks of a drive letter it does not own is a
// query — and asking for write access to C: is not a thing worth doing by
// accident.
HANDLE openVolumeForQuery(QChar driveLetter);

// The drive letters that must never be restored regardless of the bus Windows
// reports: the Windows directory's drive and the drive this executable is
// running from. C: is added by the core safety check itself.
QStringList protectedSystemDriveLetters();

// Opens a file or URL at the integrity level of the logged-in user rather than
// this process's. Handing a path to QDesktopServices from a process running as
// Administrator launches the handler — a text editor, a web browser — elevated
// too, which is not something to do to a browser. Routing it through the
// already-running explorer.exe drops it back to medium integrity.
bool openAsInvokingUser(const QString &target);

} // namespace usbrestore

#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// The GUI's side of the boundary described in docs/polkit-helper.md: starts
// usb-restoration-helper through pkexec, turns its output back into
// RestoreReporter calls, and turns its exit status back into a message.
//
// The counterpart of src/helper/main.cpp. Nothing here holds privilege — that
// is the point — so this code cannot check anything about the disk that
// matters. It does not try to. Every safety decision is made on the far side.

// Where the helper is installed. Compiled in rather than looked for: the polkit
// action authorises one absolute path, and pkexec refuses anything else, so a
// path this end went looking for could only ever be the wrong one.
QString helperExecutablePath();

// Whether a restore can be started without already being root: the helper is
// installed where it was configured to be, and pkexec exists to authorise it.
bool isHelperAvailable();

bool runHelperRestore(const RestoreRequest &request,
                      RestoreReporter &reporter,
                      RestoreResult *result,
                      QString *error);

} // namespace usbrestore

#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// Holds no privilege, so every safety decision is left to the helper.

// Compiled in rather than searched for: the polkit action authorises one absolute path.
QString helperExecutablePath();

bool isHelperAvailable();

bool runHelperRestore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error);

} // namespace usbrestore

#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// Runs in the root GUI or in the helper, so it re-derives every fact it relies on:
// the caller supplies a request, never a conclusion.

// Not on the DiskService so the GUI and the helper ask the same question.
bool canCreateFileSystemOn(FileSystemType fileSystem, const DiskInfo &disk, QString *reason);

// Reports its steps in the order of linuxRestorePlan().
bool performLinuxRestore(const RestoreRequest &request,
                         RestoreReporter &reporter,
                         RestoreResult *result,
                         QString *error);

} // namespace usbrestore

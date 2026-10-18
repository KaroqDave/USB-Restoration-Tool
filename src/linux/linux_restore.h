#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// Runs in the root GUI or in the helper, so it re-derives every fact it relies on:
// the caller supplies a request, never a conclusion.

// How many step() calls a successful restore makes.
inline constexpr int LinuxRestoreStepCount = 10;

// "Clearing old MBR, GPT and ISO signatures"; keep in step with performLinuxRestore().
// The GUI says unplugging is safe only before this step, so late is worse than early.
inline constexpr int LinuxFirstDestructiveStep = 5;

// Not on the DiskService so the GUI and the helper ask the same question.
bool canCreateFileSystemOn(FileSystemType fileSystem, const DiskInfo &disk, QString *reason);

bool performLinuxRestore(const RestoreRequest &request,
                         RestoreReporter &reporter,
                         RestoreResult *result,
                         QString *error);

} // namespace usbrestore

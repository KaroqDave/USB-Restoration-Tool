#pragma once

#include "platform/disk_service.h"

namespace usbrestore {

// The privileged half of a Linux restore, on its own so that it can run either
// inside the GUI process (when that process is already root) or inside the
// helper described in docs/polkit-helper.md. It touches no widgets and creates
// no event loop; the only way it reports anything is through the reporter it is
// handed.
//
// It re-establishes every fact it depends on: the disk is enumerated again, the
// guard is built again, and the identity is confirmed through the open
// descriptor. That is what lets the same code be safe on both sides of a
// privilege boundary — the caller supplies a request, never a conclusion.

// How many step() calls a successful restore makes.
inline constexpr int LinuxRestoreStepCount = 10;

// Whether mkfs can produce this filesystem on this disk at all. Lives here
// rather than on the DiskService so that both halves of the privilege split ask
// the same question: the GUI before it offers the restore, and the helper again
// against the disk it re-derived for itself.
bool canCreateFileSystemOn(FileSystemType fileSystem, const DiskInfo &disk, QString *reason);

bool performLinuxRestore(const RestoreRequest &request,
                         RestoreReporter &reporter,
                         RestoreResult *result,
                         QString *error);

} // namespace usbrestore

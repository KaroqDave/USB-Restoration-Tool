#pragma once

#include "core/disk.h"

#include <QString>
#include <QStringList>

#include <cstdint>

namespace usbrestore {

struct GptLayout {
    std::uint64_t startOffset = 0;
    std::uint64_t length = 0;
};

// Smallest disk worth restoring. Anything below this cannot hold a GPT header,
// a 1 MiB-aligned partition, and a backup header with room left over, and a
// device that reports less than this is far more likely to be misreporting its
// geometry than to be a real 4 MiB USB stick.
inline constexpr std::uint64_t MinimumRestoreTargetBytes = 8ull * 1024ull * 1024ull;

// Above this, an extra confirmation is required: a 256 GB "USB disk" is much
// more often an external SSD full of backups than a boot stick to be wiped.
inline constexpr std::uint64_t LargeRestoreTargetBytes = 128ull * 1024ull * 1024ull * 1024ull;

// The phrase the user must type before the restore button becomes usable. It
// names the disk number so a phrase typed for one disk cannot arm another.
QString confirmationPhrase(quint32 diskNumber);

// The first free drive letter at or after D:. C: is never offered, and A:/B:
// are left to floppy-era device naming.
QString firstAvailableDriveLetter(quint32 logicalDrivesMask);

// Drive letters that must never be wiped whatever Windows says about the bus.
// C: is always included; callers add the real system drive and the drive the
// app itself is running from, which are usually but not necessarily C:.
bool containsProtectedDriveLetter(const DiskInfo &disk, const QStringList &extraProtectedLetters = {});

// Whether the disk may be restored at all. Every check here is a refusal, not
// a warning: a disk that fails one is never offered to the restore path.
bool isSafeRestoreTarget(const DiskInfo &disk,
                         QString *reason = nullptr,
                         const QStringList &extraProtectedLetters = {});

// Whether a freshly enumerated disk is still the disk the user selected.
// Every identifier both sides report must agree; an identifier missing from
// one side is skipped rather than treated as a match.
bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason = nullptr);

bool isLargeRestoreTarget(const DiskInfo &disk);
QString largeRestoreTargetWarning(const DiskInfo &disk);

// The single 1 MiB-aligned data partition the restore creates, leaving room
// for the primary and backup GPT headers. A zero length means the disk is too
// small to lay out safely.
GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize);

// Whether a reported sector size can be trusted for raw writes. Anything else
// would make every offset this tool computes wrong.
bool isSupportedSectorSize(quint32 sectorSize);

} // namespace usbrestore

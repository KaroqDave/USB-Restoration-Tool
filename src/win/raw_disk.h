#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QString>

#include <Windows.h>

#include <cstdint>

namespace usbrestore {

// A handle on \\.\PhysicalDriveN and the raw operations the restore performs
// through it. Every destructive call is preceded by verifyIdentity(), because
// a disk number is not a stable name for a device.
class RawDisk {
  public:
    explicit RawDisk(quint32 diskNumber);
    ~RawDisk();

    RawDisk(const RawDisk &) = delete;
    RawDisk &operator=(const RawDisk &) = delete;

    bool open(QString *error = nullptr);

    // Confirms, through the open handle, that this really is the disk the user
    // selected: same device number, still on the USB bus, same byte length,
    // same sector size, same serial number where one is reported, and
    // writable. Windows can reuse a disk number the moment a device is
    // replugged, so nothing before this point rules out having opened a
    // different drive than the one that passed the safety checks.
    bool verifyIdentity(const DiskInfo &expected, QString *error = nullptr);

    bool lock(QString *error = nullptr);
    bool allowExtendedIo(QString *error = nullptr);
    bool refreshLayout(QString *error = nullptr);

    // Overwrites the first and last few megabytes so no stale MBR, GPT, or
    // hybrid-ISO signature survives to be re-detected.
    bool clearPartitionSignatures(std::uint64_t diskSize, quint32 sectorSize, QString *error = nullptr);

    bool setRaw(QString *error = nullptr);

    // Lays down one data partition spanning the disk under the requested
    // partition style. Windows writes the table itself from this description,
    // so unlike the Linux backend there are no partition-table bytes here.
    bool createSinglePartition(PartitionStyle style,
                               std::uint64_t diskSize,
                               quint32 sectorSize,
                               QString *error = nullptr);

  private:
    bool createSingleMbrPartition(const GptLayout &layout, quint32 sectorSize, QString *error);
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error);
    QString lastError(const QString &context) const;

    quint32 m_diskNumber = 0;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    bool m_locked = false;
};

} // namespace usbrestore

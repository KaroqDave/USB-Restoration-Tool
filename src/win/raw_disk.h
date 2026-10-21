#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QString>

#include <Windows.h>

#include <cstdint>

namespace usbrestore {

// Call verifyIdentity() before anything destructive: a disk number is not a stable device name.
class RawDisk {
  public:
    explicit RawDisk(quint32 diskNumber);
    ~RawDisk();

    RawDisk(const RawDisk &) = delete;
    RawDisk &operator=(const RawDisk &) = delete;

    bool open(QString *error = nullptr);

    // Windows can reuse a disk number on replug, so this re-checks the open handle
    // (number, USB bus, size, sector size, serial, writable) against the selection.
    bool verifyIdentity(const DiskInfo &expected, QString *error = nullptr);

    bool lock(QString *error = nullptr);
    bool allowExtendedIo(QString *error = nullptr);
    bool refreshLayout(QString *error = nullptr);

    // Zeroes both ends so no stale MBR, GPT or hybrid-ISO signature is re-detected.
    bool clearPartitionSignatures(std::uint64_t diskSize, quint32 sectorSize, QString *error = nullptr);

    bool setRaw(QString *error = nullptr);

    // For a disk Windows took for a "superfloppy": an empty GPT's protective MBR overwrites the
    // boot code that guess rests on, so Windows drops the whole-disk partition and its volume.
    bool replaceWholeDiskPlaceholder(QString *error = nullptr);

    // Windows writes the table itself, so unlike on Linux no table bytes are built here.
    bool createSinglePartition(PartitionStyle style,
                               FileSystemType fileSystem,
                               std::uint64_t diskSize,
                               quint32 sectorSize,
                               QString *error = nullptr);

  private:
    bool createSingleMbrPartition(const GptLayout &layout, FileSystemType fileSystem, quint32 sectorSize,
                                  QString *error);
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error);
    bool reportsWholeDiskPartition() const;
    QString lastError(const QString &context) const;

    quint32 m_diskNumber = 0;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    bool m_locked = false;
};

} // namespace usbrestore

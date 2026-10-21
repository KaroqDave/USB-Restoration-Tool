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

    // Removes whatever partition deletion through WMI could not, such as the whole-disk partition
    // Windows invents for a "superfloppy", and confirms Windows then lists no partition at all.
    bool replaceLayoutWithEmptyGpt(QString *error = nullptr);

    // Writes refused as NOT_READY and retried before the first one landed, so the log can say so.
    int notReadyRetries() const { return m_notReadyRetries; }

    // Windows writes the table itself, so unlike on Linux no table bytes are built here.
    bool createSinglePartition(PartitionStyle style,
                               FileSystemType fileSystem,
                               std::uint64_t diskSize,
                               quint32 sectorSize,
                               QString *error = nullptr);

  private:
    bool createSingleMbrPartition(const GptLayout &layout, FileSystemType fileSystem, quint32 sectorSize,
                                  QString *error);
    bool createGptDisk(GUID *diskId, QString *error);
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error);
    bool layoutHasPartitions(bool *hasPartitions, QString *error) const;
    QString lastError(const QString &context) const;

    quint32 m_diskNumber = 0;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
    bool m_locked = false;
    bool m_writeAccepted = false;
    int m_notReadyRetries = 0;
};

} // namespace usbrestore

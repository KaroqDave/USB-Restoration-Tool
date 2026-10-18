#pragma once

#include "core/disk.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace usbrestore {

// An open file descriptor on a whole-disk block device, and the raw operations
// a restore performs through it. The Linux counterpart of RawDisk.
class BlockDevice {
  public:
    enum class Access {
        // Read-only and shared, for identifying a device before anything has
        // been unmounted.
        Inspect,
        // Read-write with O_EXCL, which the kernel refuses while any partition
        // of the disk is mounted or claimed. That refusal is a safety feature,
        // not an obstacle: it means no filesystem can be live underneath a
        // restore that has started.
        Exclusive,
    };

    explicit BlockDevice(QString devicePath);
    ~BlockDevice();

    BlockDevice(const BlockDevice &) = delete;
    BlockDevice &operator=(const BlockDevice &) = delete;

    bool open(Access access, QString *error = nullptr);
    void close();

    // Confirms, through the open descriptor, that this is still the disk the
    // user selected: same device number, same byte length, same sector size,
    // and not read-only. A device node can be re-created pointing somewhere
    // else between one open and the next, so this is asked of the descriptor
    // rather than of the path.
    bool verifyIdentity(const DiskInfo &expected, QString *error = nullptr);

    // The kernel's sequence number for the disk behind this descriptor, which
    // is never reused: a stick unplugged and replaced by another gets a new one
    // even when it takes the same name and major:minor. Asked through a
    // partition, it answers for the partition's disk. False on kernels older
    // than 5.15, which do not have it.
    bool diskSequence(std::uint64_t *sequence) const;

    // Confirms, through the open descriptor, that this node is a partition of
    // the whole disk `diskName` — and, when a sequence is given, of the very
    // disk instance that carried it rather than one that took its name since.
    bool verifyPartitionOf(const QString &diskName,
                           const std::optional<std::uint64_t> &expectedSequence,
                           QString *error = nullptr) const;

    std::uint64_t sizeInBytes() const;
    quint32 logicalSectorSize() const;

    bool writeAt(std::uint64_t offset, const QByteArray &data, QString *error = nullptr);
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error = nullptr);
    bool flush(QString *error = nullptr);

    // Asks the kernel to drop the old partition table and read the new one.
    bool rereadPartitionTable(QString *error = nullptr);

  private:
    QString errorText(const QString &context, int errorNumber) const;

    QString m_devicePath;
    int m_fd = -1;
};

} // namespace usbrestore

#pragma once

#include "core/disk.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace usbrestore {

// A syscall on a device failed. errno is the fact; the wording is for the person
// holding the drive, so it names the device, says what went wrong, and what to do next.
QString deviceErrorText(const QString &context, int errorNumber);

// The Linux counterpart of RawDisk.
class BlockDevice {
  public:
    enum class Access {
        // Read-only and shared, usable before anything has been unmounted.
        Inspect,
        // O_EXCL fails while any partition is mounted or claimed, so no
        // filesystem can be live underneath a started restore.
        Exclusive,
    };

    explicit BlockDevice(QString devicePath);
    ~BlockDevice();

    BlockDevice(const BlockDevice &) = delete;
    BlockDevice &operator=(const BlockDevice &) = delete;

    bool open(Access access, QString *error = nullptr);
    void close();

    // Asked of the descriptor, not the path: a device node can be re-created
    // pointing elsewhere between one open and the next.
    bool verifyIdentity(const DiskInfo &expected, QString *error = nullptr);

    // Never reused, so a replacement stick gets a new one even under the same
    // name. False before kernel 5.15.
    bool diskSequence(std::uint64_t *sequence) const;

    // Whether this node is a partition of `diskName`, and of the disk instance
    // with `expectedSequence` when one is given.
    bool verifyPartitionOf(
        const QString &diskName, const std::optional<std::uint64_t> &expectedSequence, QString *error = nullptr) const;

    std::uint64_t sizeInBytes() const;
    quint32 logicalSectorSize() const;
    int descriptor() const { return m_fd; }

    bool writeAt(std::uint64_t offset, const QByteArray &data, QString *error = nullptr);
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error = nullptr);
    bool flush(QString *error = nullptr);

    bool rereadPartitionTable(QString *error = nullptr);

  private:
    QString m_devicePath;
    int m_fd = -1;
};

} // namespace usbrestore

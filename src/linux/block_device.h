#pragma once

#include "core/disk.h"

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>

namespace usbrestore {

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

    // Never reused, even when a replacement stick takes the same name and
    // major:minor. Through a partition it answers for its disk. False before 5.15.
    bool diskSequence(std::uint64_t *sequence) const;

    // With a sequence, also proves it is the same disk instance, not one that took its name since.
    bool verifyPartitionOf(const QString &diskName,
                           const std::optional<std::uint64_t> &expectedSequence,
                           QString *error = nullptr) const;

    std::uint64_t sizeInBytes() const;
    quint32 logicalSectorSize() const;

    bool writeAt(std::uint64_t offset, const QByteArray &data, QString *error = nullptr);
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error = nullptr);
    bool flush(QString *error = nullptr);

    bool rereadPartitionTable(QString *error = nullptr);

  private:
    QString errorText(const QString &context, int errorNumber) const;

    QString m_devicePath;
    int m_fd = -1;
};

} // namespace usbrestore

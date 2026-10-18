#include "linux/block_device.h"

#include "core/safety.h"
#include "linux/sysfs.h"

#include <QFileInfo>
#include <QtGlobal>

#include <fcntl.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <utility>

// Missing from headers older than 5.15; the ioctl then just fails.
#ifndef BLKGETDISKSEQ
#define BLKGETDISKSEQ _IOR(0x12, 128, __u64)
#endif

namespace usbrestore {

namespace {

constexpr std::uint64_t WriteChunkBytes = 1024ull * 1024ull;

} // namespace

BlockDevice::BlockDevice(QString devicePath) : m_devicePath(std::move(devicePath)) {}

BlockDevice::~BlockDevice()
{
    close();
}

void BlockDevice::close()
{
    if (m_fd >= 0) {
        ::close(m_fd);
        m_fd = -1;
    }
}

QString BlockDevice::errorText(const QString &context, int errorNumber) const
{
    return QStringLiteral("%1: %2 (errno %3)")
        .arg(context, QString::fromLocal8Bit(std::strerror(errorNumber)))
        .arg(errorNumber);
}

bool BlockDevice::open(Access access, QString *error)
{
    close();

    const int flags = access == Access::Exclusive ? (O_RDWR | O_EXCL | O_CLOEXEC | O_SYNC)
                                                  : (O_RDONLY | O_CLOEXEC);
    m_fd = ::open(m_devicePath.toLocal8Bit().constData(), flags);
    if (m_fd < 0) {
        const int errorNumber = errno;
        if (error) {
            if (access == Access::Exclusive && errorNumber == EBUSY) {
                *error = QStringLiteral("%1 is still in use. Close anything reading from the drive, make sure every "
                                        "partition is unmounted, and try again.")
                             .arg(m_devicePath);
            } else {
                *error = errorText(QStringLiteral("Could not open %1").arg(m_devicePath), errorNumber);
            }
        }
        return false;
    }
    return true;
}

std::uint64_t BlockDevice::sizeInBytes() const
{
    if (m_fd < 0) {
        return 0;
    }
    std::uint64_t size = 0;
    if (::ioctl(m_fd, BLKGETSIZE64, &size) != 0) {
        return 0;
    }
    return size;
}

quint32 BlockDevice::logicalSectorSize() const
{
    if (m_fd < 0) {
        return 0;
    }
    int sectorSize = 0;
    if (::ioctl(m_fd, BLKSSZGET, &sectorSize) != 0) {
        return 0;
    }
    return static_cast<quint32>(sectorSize);
}

bool BlockDevice::verifyIdentity(const DiskInfo &expected, QString *error)
{
    const auto refuse = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return false;
    };

    if (m_fd < 0) {
        return refuse(QStringLiteral("The block device is not open."));
    }

    struct stat status = {};
    if (::fstat(m_fd, &status) != 0) {
        return refuse(errorText(QStringLiteral("Could not stat %1").arg(m_devicePath), errno));
    }
    if (!S_ISBLK(status.st_mode)) {
        return refuse(QStringLiteral("%1 is not a block device.").arg(m_devicePath));
    }

    // Only whole disks appear directly under /sys/block, so a missing entry
    // means the path is a partition, which must never get a partition table.
    const QString diskName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber = readDeviceNumber(QStringLiteral("/sys/block/%1").arg(diskName));
    if (!expectedNumber.isValid()) {
        return refuse(QStringLiteral("%1 is not a whole disk.").arg(m_devicePath));
    }

    // Catches a node re-created since the scan to point somewhere else.
    const auto actualMajor = static_cast<quint32>(major(status.st_rdev));
    const auto actualMinor = static_cast<quint32>(minor(status.st_rdev));
    if (actualMajor != expectedNumber.major || actualMinor != expectedNumber.minor) {
        return refuse(QStringLiteral("%1 no longer points at the disk that was selected.").arg(m_devicePath));
    }

    const std::uint64_t reportedSize = sizeInBytes();
    if (reportedSize == 0) {
        return refuse(QStringLiteral("%1 reports no usable media.").arg(m_devicePath));
    }
    if (expected.size != 0 && reportedSize != expected.size) {
        return refuse(QStringLiteral("%1 now reports %2 instead of the %3 that was selected.")
                          .arg(m_devicePath, formatByteSize(reportedSize), formatByteSize(expected.size)));
    }

    const quint32 reportedSector = logicalSectorSize();
    if (reportedSector != 0 && expected.sectorSize != 0 && reportedSector != expected.sectorSize) {
        return refuse(QStringLiteral("%1 reports %2-byte sectors instead of the %3-byte sectors that were selected.")
                          .arg(m_devicePath)
                          .arg(reportedSector)
                          .arg(expected.sectorSize));
    }

    if (readSysfsAttribute(QStringLiteral("/sys/block/%1/ro").arg(diskName)) == QStringLiteral("1")) {
        return refuse(QStringLiteral("%1 is write-protected.").arg(m_devicePath));
    }

    // Re-read rather than trusting the scan, which a swapped device would have passed.
    if (!isUsbBlockDevice(diskName)) {
        return refuse(QStringLiteral("%1 is not on the USB bus.").arg(m_devicePath));
    }

    return true;
}

bool BlockDevice::diskSequence(std::uint64_t *sequence) const
{
    if (m_fd < 0) {
        return false;
    }
    std::uint64_t value = 0;
    if (::ioctl(m_fd, BLKGETDISKSEQ, &value) != 0 || value == 0) {
        return false;
    }
    if (sequence) {
        *sequence = value;
    }
    return true;
}

bool BlockDevice::verifyPartitionOf(const QString &diskName,
                                    const std::optional<std::uint64_t> &expectedSequence,
                                    QString *error) const
{
    const auto refuse = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return false;
    };

    if (m_fd < 0) {
        return refuse(QStringLiteral("%1 is not open.").arg(m_devicePath));
    }

    struct stat status = {};
    if (::fstat(m_fd, &status) != 0) {
        return refuse(errorText(QStringLiteral("Could not stat %1").arg(m_devicePath), errno));
    }
    if (!S_ISBLK(status.st_mode)) {
        return refuse(QStringLiteral("%1 is not a block device.").arg(m_devicePath));
    }

    // Only the owning disk lists the partition under /sys/block/<disk>/.
    const QString partitionName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber =
        readDeviceNumber(QStringLiteral("/sys/block/%1/%2").arg(diskName, partitionName));
    const auto actualMajor = static_cast<quint32>(major(status.st_rdev));
    const auto actualMinor = static_cast<quint32>(minor(status.st_rdev));
    if (!expectedNumber.isValid() || actualMajor != expectedNumber.major || actualMinor != expectedNumber.minor) {
        return refuse(QStringLiteral("%1 is not a partition of /dev/%2.").arg(m_devicePath, diskName));
    }

    if (expectedSequence) {
        std::uint64_t actualSequence = 0;
        if (!diskSequence(&actualSequence) || actualSequence != *expectedSequence) {
            return refuse(QStringLiteral("%1 belongs to a different disk than the one the partition table was written "
                                         "to.")
                              .arg(m_devicePath));
        }
    }

    return true;
}

bool BlockDevice::writeAt(std::uint64_t offset, const QByteArray &data, QString *error)
{
    if (m_fd < 0) {
        if (error) {
            *error = QStringLiteral("The block device is not open.");
        }
        return false;
    }

    qint64 written = 0;
    while (written < data.size()) {
        const ssize_t result =
            ::pwrite(m_fd, data.constData() + written, static_cast<size_t>(data.size() - written),
                     static_cast<off_t>(offset + static_cast<std::uint64_t>(written)));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error) {
                *error = errorText(QStringLiteral("Could not write to %1").arg(m_devicePath), errno);
            }
            return false;
        }
        if (result == 0) {
            if (error) {
                *error = QStringLiteral("%1 stopped accepting writes.").arg(m_devicePath);
            }
            return false;
        }
        written += result;
    }
    return true;
}

bool BlockDevice::writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error)
{
    if (bytes == 0) {
        return true;
    }

    const QString unaligned = unalignedZeroRangeError(offset, bytes, sectorSize);
    if (!unaligned.isEmpty()) {
        if (error) {
            *error = unaligned;
        }
        return false;
    }

    // A 4Kn disk rejects any raw write that is not a sector multiple.
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    const auto chunk = static_cast<int>((WriteChunkBytes / sector) * sector);
    const QByteArray zeros(chunk > 0 ? chunk : static_cast<int>(sector), '\0');

    std::uint64_t remaining = bytes;
    std::uint64_t cursor = offset;
    while (remaining > 0) {
        const auto count = static_cast<int>(qMin<std::uint64_t>(remaining, static_cast<std::uint64_t>(zeros.size())));
        if (!writeAt(cursor, count == zeros.size() ? zeros : zeros.left(count), error)) {
            return false;
        }
        cursor += static_cast<std::uint64_t>(count);
        remaining -= static_cast<std::uint64_t>(count);
    }
    return true;
}

bool BlockDevice::flush(QString *error)
{
    if (m_fd < 0) {
        return true;
    }
    if (::fsync(m_fd) != 0) {
        if (error) {
            *error = errorText(QStringLiteral("Could not flush writes to %1").arg(m_devicePath), errno);
        }
        return false;
    }
    // Drop the cached copy so the partition scan reads the new table, not the old one.
    ::ioctl(m_fd, BLKFLSBUF, 0);
    return true;
}

bool BlockDevice::rereadPartitionTable(QString *error)
{
    if (m_fd < 0) {
        if (error) {
            *error = QStringLiteral("The block device is not open.");
        }
        return false;
    }

    if (::ioctl(m_fd, BLKRRPART, 0) != 0) {
        if (error) {
            *error = errorText(QStringLiteral("Could not make the kernel re-read the partition table on %1")
                                   .arg(m_devicePath),
                               errno);
        }
        return false;
    }
    return true;
}

} // namespace usbrestore

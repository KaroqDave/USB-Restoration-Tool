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
                // The kernel refuses an exclusive open while something still
                // holds the disk. Saying which of the two it is saves the user
                // guessing.
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

    // Only whole disks appear directly under /sys/block; a partition lives one
    // level down, inside its disk's directory. So a missing entry here means
    // the path names something that is not a whole disk, and writing a
    // partition table into a partition would be a very bad day.
    const QString diskName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber = readDeviceNumber(QStringLiteral("/sys/block/%1").arg(diskName));
    if (!expectedNumber.isValid()) {
        return refuse(QStringLiteral("%1 is not a whole disk.").arg(m_devicePath));
    }

    // The device node's major:minor is compared against the pair sysfs reports
    // for that disk. A node re-created between the scan and here, pointing
    // somewhere else, would not match.
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

    // Re-read the bus rather than trusting the enumeration: this is the check a
    // device swapped between the scan and now cannot get past.
    if (!isUsbBlockDevice(diskName)) {
        return refuse(QStringLiteral("%1 is not on the USB bus.").arg(m_devicePath));
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

    // A sector multiple, because a raw write to a block device that is not one
    // is rejected outright on a 4 Kn disk.
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
    // Drops the kernel's cached copy of the disk, so the partition scan that
    // follows reads the table that was just written rather than the old one.
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

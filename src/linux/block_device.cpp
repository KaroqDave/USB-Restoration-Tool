#include "linux/block_device.h"

#include "core/safety.h"
#include "linux/sysfs.h"
#include "linux/linux_enumerate.h"

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

QString deviceErrorText(const QString &context, int errorNumber)
{
    // strerror() is a code the user cannot act on; these are the cases that
    // actually happen on a USB stick and what fixes them.
    QString reason;
    switch (errorNumber) {
    case EBUSY:
        reason = QStringLiteral(
            "another program still has it open. Close that program and unmount every partition "
            "on the drive");
        break;
    case EACCES:
    case EPERM:
        reason = QStringLiteral(
            "the system refused it. USB Restoration Tool needs permission to write to the drive: "
            "answer the privilege prompt, or run it as root");
        break;
    case EIO:
        reason = QStringLiteral("the drive stopped answering. Unplug it, plug it back in and try again");
        break;
    case ENXIO:
    case ENODEV:
        reason = QStringLiteral("the drive is no longer there. Unplug it, plug it back in and refresh the list");
        break;
    case ENOSPC:
        reason = QStringLiteral("the drive ran out of room while writing");
        break;
    default:
        reason = QString::fromLocal8Bit(std::strerror(errorNumber));
    }
    return QStringLiteral("%1: %2 (errno %3)").arg(context, reason).arg(errorNumber);
}

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

bool BlockDevice::open(Access access, QString *error)
{
    close();

    const int flags = access == Access::Exclusive ? (O_RDWR | O_EXCL | O_CLOEXEC | O_SYNC) : (O_RDONLY | O_CLOEXEC);
    m_fd = ::open(m_devicePath.toLocal8Bit().constData(), flags);
    if (m_fd < 0) {
        const int errorNumber = errno;
        if (error) {
            if (access == Access::Exclusive && errorNumber == EBUSY) {
                *error = QStringLiteral(
                             "%1 is still in use. Close anything reading from the drive, make sure every "
                             "partition is unmounted, and try again.")
                             .arg(m_devicePath);
            } else {
                *error = deviceErrorText(QStringLiteral("Could not open %1").arg(m_devicePath), errorNumber);
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
        return refuse(QStringLiteral("%1 is not open, so nothing could be checked on it.").arg(m_devicePath));
    }

    struct stat status = {};
    if (::fstat(m_fd, &status) != 0) {
        return refuse(
            deviceErrorText(QStringLiteral("Could not read the device information of %1").arg(m_devicePath), errno));
    }
    if (!S_ISBLK(status.st_mode)) {
        return refuse(QStringLiteral("%1 is not a disk device.").arg(m_devicePath));
    }

    std::uint64_t sequence = 0;
    if (!expected.deviceSequence || !diskSequence(&sequence)) {
        return refuse(QStringLiteral(
                          "%1 does not report which instance of the drive it is, so the tool cannot be sure this is "
                          "the disk you selected. Linux restores need kernel 5.15 or newer (check with `uname -r`).")
                          .arg(m_devicePath));
    }
    if (!isSameDeviceSequence(m_devicePath, expected.deviceSequence, sequence, error)) {
        return false;
    }

    // Only whole disks appear directly under /sys/block. A missing entry means
    // this path is not the selected whole-disk node; do not assume which node it is.
    const QString diskName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber = readDeviceNumber(QStringLiteral("/sys/block/%1").arg(diskName));
    if (!expectedNumber.isValid()) {
        return refuse(QStringLiteral(
                          "%1 is not listed under /sys/block as a whole disk. Select the drive itself, such as "
                          "/dev/sdb rather than /dev/sdb1.")
                          .arg(m_devicePath));
    }

    // Catches a node re-created since the scan to point somewhere else.
    const auto actualMajor = static_cast<quint32>(major(status.st_rdev));
    const auto actualMinor = static_cast<quint32>(minor(status.st_rdev));
    if (actualMajor != expectedNumber.major || actualMinor != expectedNumber.minor) {
        return refuse(
            QStringLiteral("%1 no longer points at the disk that was selected. Refresh and select the USB again.")
                .arg(m_devicePath));
    }

    const std::uint64_t reportedSize = sizeInBytes();
    if (reportedSize == 0) {
        return refuse(QStringLiteral(
                          "%1 reports no usable media. The drive may be empty, damaged, or not fully "
                          "plugged in.")
                          .arg(m_devicePath));
    }
    if (expected.size != 0 && reportedSize != expected.size) {
        return refuse(QStringLiteral(
                          "%1 now holds a %2 drive, not the %3 that was selected. Refresh and select the "
                          "USB again.")
                          .arg(m_devicePath, formatByteSize(reportedSize), formatByteSize(expected.size)));
    }

    const quint32 reportedSector = logicalSectorSize();
    if (reportedSector == 0 || reportedSector != expected.sectorSize) {
        return refuse(QStringLiteral(
                          "%1 uses %2-byte sectors, not the %3-byte sectors the layout was built for. "
                          "Refresh and select the USB again.")
                          .arg(m_devicePath)
                          .arg(reportedSector)
                          .arg(expected.sectorSize));
    }

    if (readSysfsAttribute(QStringLiteral("/sys/block/%1/ro").arg(diskName)) == QStringLiteral("1")) {
        return refuse(QStringLiteral(
                          "%1 is write-protected and cannot be formatted. Check for a write-protect switch on the "
                          "drive.")
                          .arg(m_devicePath));
    }

    // Re-read rather than trusting the scan, which a swapped device would have passed.
    if (!isUsbBlockDevice(diskName)) {
        return refuse(QStringLiteral("%1 is not on the USB bus.").arg(m_devicePath));
    }

    // Also compares the sequence, so with the handle's checked above, sysfs names the disk this handle holds.
    return isSameRestoreTarget(expected, diskInfoFor(diskName), error);
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

bool BlockDevice::verifyPartitionOf(
    const QString &diskName, const std::optional<std::uint64_t> &expectedSequence, QString *error) const
{
    const auto refuse = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return false;
    };

    if (m_fd < 0) {
        return refuse(QStringLiteral("%1 is not open, so the new partition could not be checked.").arg(m_devicePath));
    }

    struct stat status = {};
    if (::fstat(m_fd, &status) != 0) {
        return refuse(
            deviceErrorText(QStringLiteral("Could not read the device information of %1").arg(m_devicePath), errno));
    }
    if (!S_ISBLK(status.st_mode)) {
        return refuse(QStringLiteral("%1 is not a disk device.").arg(m_devicePath));
    }

    // Only the owning disk lists the partition under /sys/block/<disk>/.
    const QString partitionName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber =
        readDeviceNumber(QStringLiteral("/sys/block/%1/%2").arg(diskName, partitionName));
    const auto actualMajor = static_cast<quint32>(major(status.st_rdev));
    const auto actualMinor = static_cast<quint32>(minor(status.st_rdev));
    if (!expectedNumber.isValid() || actualMajor != expectedNumber.major || actualMinor != expectedNumber.minor) {
        return refuse(QStringLiteral(
                          "%1 did not appear as a partition of /dev/%2. Unplug the drive, plug it back in and try "
                          "again.")
                          .arg(m_devicePath, diskName));
    }

    if (expectedSequence) {
        std::uint64_t actualSequence = 0;
        if (!diskSequence(&actualSequence) || actualSequence != *expectedSequence) {
            return refuse(QStringLiteral(
                              "%1 is not the drive the partition table was written to: a different disk took its "
                              "place during the restore.")
                              .arg(m_devicePath));
        }
    }

    return true;
}

bool BlockDevice::writeAt(std::uint64_t offset, const QByteArray &data, QString *error)
{
    if (m_fd < 0) {
        if (error) {
            *error = QStringLiteral("%1 is not open, so nothing could be written to it.").arg(m_devicePath);
        }
        return false;
    }

    qint64 written = 0;
    while (written < data.size()) {
        const ssize_t result = ::pwrite(
            m_fd,
            data.constData() + written,
            static_cast<size_t>(data.size() - written),
            static_cast<off_t>(offset + static_cast<std::uint64_t>(written)));
        if (result < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (error) {
                *error = deviceErrorText(QStringLiteral("Could not write to %1").arg(m_devicePath), errno);
            }
            return false;
        }
        if (result == 0) {
            if (error) {
                *error = QStringLiteral(
                             "%1 stopped accepting data part way through the write. Unplug the drive, "
                             "plug it back in and try again; if it keeps happening the drive is damaged.")
                             .arg(m_devicePath);
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
            *error = deviceErrorText(QStringLiteral("Could not save the changes to %1").arg(m_devicePath), errno);
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
            *error = QStringLiteral("%1 is not open, so its partition table could not be re-read.").arg(m_devicePath);
        }
        return false;
    }

    if (::ioctl(m_fd, BLKRRPART, 0) != 0) {
        if (error) {
            *error = deviceErrorText(
                QStringLiteral("The new partition table was written to %1, but the system would not re-scan it")
                    .arg(m_devicePath),
                errno);
        }
        return false;
    }
    return true;
}

} // namespace usbrestore

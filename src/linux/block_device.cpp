#include "linux/block_device.h"

#include "core/safety.h"
#include "linux/linux_enumerate.h"

#include <QFileInfo>
#include <QtGlobal>

#include <fcntl.h>
#include <linux/fs.h>
#include <linux/loop.h>
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

// O_SYNC flushes each write, so clearing a 4 MiB signature area should take one request.
constexpr std::uint64_t WriteChunkBytes = 4ull * 1024ull * 1024ull;

} // namespace

RestoreMessage deviceErrorMessage(const RestoreMessage &context, int errorNumber)
{
    // strerror() is a code the user cannot act on; these are the cases that
    // actually happen on a USB stick and what fixes them.
    RestoreMessage reason;
    RestoreMessage advice =
        restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Unplug the drive, plug it back in and try again"));
    switch (errorNumber) {
    case EBUSY:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "another program still has it open"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend", "Close that program, unmount every partition on the drive and try again"));
        break;
    // The kernel refuses writes to a read-only disk with EPERM, and worn sticks make themselves read-only.
    case EACCES:
    case EPERM:
    case EROFS:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive would not accept writes"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "If it has a write-protect switch, turn it off and try again. If it has none, the drive may have made "
            "itself read-only, which worn drives do, and needs replacing"));
        break;
    case EIO:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive stopped answering"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Unplug it, plug it back in and try again; if it keeps happening the drive is damaged"));
        break;
    case ENXIO:
    case ENODEV:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive is no longer there"));
        advice = restoreText(
            QT_TRANSLATE_NOOP("usbrestore::Backend", "Plug it back in, refresh the list and select it again"));
        break;
    // Every write lies inside the size the drive reported, so running out of room means that size was wrong.
    case ENOSPC:
        reason =
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive ran out of room before its reported size"));
        advice = restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Unplug it, plug it back in and try again; if it happens again the drive is smaller than it claims"));
        break;
    default:
        reason = QString::fromLocal8Bit(std::strerror(errorNumber));
    }
    return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1: %2 (errno %3). %4."))
        .arg(context, reason, errorNumber, advice);
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

bool BlockDevice::open(Access access, QString *error, QString *displayError)
{
    close();

    const int flags = access == Access::Exclusive ? (O_RDWR | O_EXCL | O_CLOEXEC | O_SYNC) : (O_RDONLY | O_CLOEXEC);
    m_fd = ::open(m_devicePath.toLocal8Bit().constData(), flags);
    if (m_fd < 0) {
        const int errorNumber = errno;
        if (access == Access::Exclusive && errorNumber == EBUSY) {
            setRestoreError(
                error,
                displayError,
                restoreText(QT_TRANSLATE_NOOP(
                                "usbrestore::Backend",
                                "%1 is still in use. Close anything reading from the drive, make sure every "
                                "partition is unmounted, and try again."))
                    .arg(m_devicePath));
        } else {
            setRestoreError(
                error,
                displayError,
                deviceErrorMessage(
                    restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not open %1")).arg(m_devicePath),
                    errorNumber));
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

bool BlockDevice::blockDeviceNumber(DeviceNumber *number, QString *error, QString *displayError) const
{
    const auto refuse = [error, displayError](const RestoreMessage &text) {
        setRestoreError(error, displayError, text);
        return false;
    };

    struct stat status = {};
    if (::fstat(m_fd, &status) != 0) {
        const int errorNumber = errno;
        return refuse(deviceErrorMessage(
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not read the device information of %1"))
                .arg(m_devicePath),
            errorNumber));
    }
    if (!S_ISBLK(status.st_mode)) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1 is not a disk device.")).arg(m_devicePath));
    }
    number->major = static_cast<quint32>(major(status.st_rdev));
    number->minor = static_cast<quint32>(minor(status.st_rdev));
    return true;
}

bool BlockDevice::verifyIdentity(const DiskInfo &expected, QString *error, QString *displayError)
{
    const auto refuse = [error, displayError](const RestoreMessage &text) {
        setRestoreError(error, displayError, text);
        return false;
    };

    if (m_fd < 0) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1 is not open, so nothing could be checked on it."))
                .arg(m_devicePath));
    }

    DeviceNumber actual;
    if (!blockDeviceNumber(&actual, error, displayError)) {
        return false;
    }

    std::uint64_t sequence = 0;
    if (!expected.deviceSequence || !diskSequence(&sequence)) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "%1 does not report which instance of the drive it is, so the tool cannot be sure this is "
                            "the disk you selected. Linux restores need kernel 5.15 or newer (check with `uname -r`)."))
                .arg(m_devicePath));
    }
    if (!checkRestoreError(error, displayError, [&](QString *text, TextMode mode) {
            return isSameDeviceSequence(m_devicePath, expected.deviceSequence, sequence, text, mode);
        })) {
        return false;
    }

    // Only whole disks appear directly under /sys/block. A missing entry means
    // this path is not the selected whole-disk node; do not assume which node it is.
    const QString diskName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber = readDeviceNumber(QStringLiteral("/sys/block/%1").arg(diskName));
    if (!expectedNumber.isValid()) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "%1 is not listed under /sys/block as a whole disk. Select the drive itself, such as "
                            "/dev/sdb rather than /dev/sdb1."))
                .arg(m_devicePath));
    }

    // Catches a node re-created since the scan to point somewhere else.
    if (actual != expectedNumber) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "%1 no longer points at the disk that was selected. Refresh and select the USB again."))
                .arg(m_devicePath));
    }

    const std::uint64_t reportedSize = sizeInBytes();
    if (reportedSize == 0) {
        return refuse(restoreText(QT_TRANSLATE_NOOP(
                                      "usbrestore::Backend",
                                      "%1 reports no usable media. The drive may be empty, damaged, or not fully "
                                      "plugged in."))
                          .arg(m_devicePath));
    }
    if (expected.size != 0 && reportedSize != expected.size) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "%1 now holds a %2 drive, not the %3 that was selected. Refresh and select the "
                            "USB again."))
                .arg(m_devicePath, formatByteSizeMessage(reportedSize), formatByteSizeMessage(expected.size)));
    }

    const quint32 reportedSector = logicalSectorSize();
    if (reportedSector == 0 || reportedSector != expected.sectorSize) {
        return refuse(restoreText(QT_TRANSLATE_NOOP(
                                      "usbrestore::Backend",
                                      "%1 uses %2-byte sectors, not the %3-byte sectors the layout was built for. "
                                      "Refresh and select the USB again."))
                          .arg(m_devicePath, reportedSector, expected.sectorSize));
    }

    if (readSysfsAttribute(QStringLiteral("/sys/block/%1/ro").arg(diskName)) == QStringLiteral("1")) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "%1 is write-protected and cannot be formatted. Check for a write-protect switch on the "
                            "drive."))
                .arg(m_devicePath));
    }

    // Re-read rather than trusting the scan, which a swapped device would have passed.
    if (!isUsbBlockDevice(diskName)) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1 is not on the USB bus.")).arg(m_devicePath));
    }

    // Also compares the sequence, so with the handle's checked above, sysfs names the disk this handle holds.
    const DiskInfo current = diskInfoFor(diskName, DiskDetails::Restore);
    return checkRestoreError(error, displayError, [&](QString *text, TextMode mode) {
        return isSameRestoreTarget(expected, current, text, mode);
    });
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
    const QString &diskName,
    const std::optional<std::uint64_t> &expectedSequence,
    QString *error,
    QString *displayError) const
{
    const auto refuse = [error, displayError](const RestoreMessage &text) {
        setRestoreError(error, displayError, text);
        return false;
    };

    if (m_fd < 0) {
        return refuse(
            restoreText(
                QT_TRANSLATE_NOOP("usbrestore::Backend", "%1 is not open, so the new partition could not be checked."))
                .arg(m_devicePath));
    }

    DeviceNumber actual;
    if (!blockDeviceNumber(&actual, error, displayError)) {
        return false;
    }

    // Only the owning disk lists the partition under /sys/block/<disk>/.
    const QString partitionName = QFileInfo(m_devicePath).fileName();
    const DeviceNumber expectedNumber =
        readDeviceNumber(QStringLiteral("/sys/block/%1/%2").arg(diskName, partitionName));
    if (!expectedNumber.isValid() || actual != expectedNumber) {
        return refuse(
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend",
                            "%1 did not appear as a partition of /dev/%2. Unplug the drive, plug it back in and try "
                            "again."))
                .arg(m_devicePath, diskName));
    }

    if (expectedSequence) {
        std::uint64_t actualSequence = 0;
        if (!diskSequence(&actualSequence) || actualSequence != *expectedSequence) {
            return refuse(
                restoreText(QT_TRANSLATE_NOOP(
                                "usbrestore::Backend",
                                "%1 is not the drive the partition table was written to: a different disk took its "
                                "place during the restore."))
                    .arg(m_devicePath));
        }
    }

    return true;
}

bool BlockDevice::writeAt(std::uint64_t offset, const QByteArray &data, QString *error, QString *displayError)
{
    if (m_fd < 0) {
        setRestoreError(
            error,
            displayError,
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1 is not open, so nothing could be written to it."))
                .arg(m_devicePath));
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
            const int errorNumber = errno;
            setRestoreError(
                error,
                displayError,
                deviceErrorMessage(
                    restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not write to %1")).arg(m_devicePath),
                    errorNumber));
            return false;
        }
        if (result == 0) {
            setRestoreError(
                error,
                displayError,
                restoreText(QT_TRANSLATE_NOOP(
                                "usbrestore::Backend",
                                "%1 stopped accepting data part way through the write. Unplug the drive, "
                                "plug it back in and try again; if it keeps happening the drive is damaged."))
                    .arg(m_devicePath));
            return false;
        }
        written += result;
    }
    return true;
}

bool BlockDevice::writeZeros(
    std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error, QString *displayError)
{
    if (bytes == 0) {
        return true;
    }

    const RestoreMessage unaligned{
        unalignedZeroRangeError(offset, bytes, sectorSize),
        unalignedZeroRangeError(offset, bytes, sectorSize, TextMode::Display)};
    if (!unaligned.english.isEmpty()) {
        setRestoreError(error, displayError, unaligned);
        return false;
    }

    // A 4Kn disk rejects any raw write that is not a sector multiple.
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    const auto chunk = static_cast<int>((qMin(WriteChunkBytes, bytes) / sector) * sector);
    const QByteArray zeros(chunk > 0 ? chunk : static_cast<int>(sector), '\0');

    std::uint64_t remaining = bytes;
    std::uint64_t cursor = offset;
    while (remaining > 0) {
        const auto count = static_cast<int>(qMin<std::uint64_t>(remaining, static_cast<std::uint64_t>(zeros.size())));
        if (!writeAt(cursor, count == zeros.size() ? zeros : zeros.left(count), error, displayError)) {
            return false;
        }
        cursor += static_cast<std::uint64_t>(count);
        remaining -= static_cast<std::uint64_t>(count);
    }
    return true;
}

bool BlockDevice::flush(QString *error, QString *displayError)
{
    if (m_fd < 0) {
        return true;
    }
    if (::fsync(m_fd) != 0) {
        const int errorNumber = errno;
        setRestoreError(
            error,
            displayError,
            deviceErrorMessage(
                restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not save the changes to %1"))
                    .arg(m_devicePath),
                errorNumber));
        return false;
    }
    // Drop the cached copy so the partition scan reads the new table, not the old one.
    ::ioctl(m_fd, BLKFLSBUF, 0);
    return true;
}

bool BlockDevice::rereadPartitionTable(QString *error, QString *displayError)
{
    if (m_fd < 0) {
        setRestoreError(
            error,
            displayError,
            restoreText(QT_TRANSLATE_NOOP(
                            "usbrestore::Backend", "%1 is not open, so its partition table could not be re-read."))
                .arg(m_devicePath));
        return false;
    }

    if (::ioctl(m_fd, BLKRRPART, 0) != 0) {
        const int errorNumber = errno;
        setRestoreError(
            error,
            displayError,
            deviceErrorMessage(
                restoreText(QT_TRANSLATE_NOOP(
                                "usbrestore::Backend",
                                "The new partition table was written to %1, but the system would not re-scan it"))
                    .arg(m_devicePath),
                errorNumber));
        return false;
    }
    return true;
}

// A loop-control or mapping failure is a kernel loop-driver problem, not a USB
// stick problem, except EIO, which comes from the drive underneath. The caller supplies
// the action, without a final full stop.
RestoreMessage loopErrorMessage(const RestoreMessage &operation, int errorNumber, const RestoreMessage &advice)
{
    RestoreMessage reason;
    switch (errorNumber) {
    case EBUSY:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the loop driver is busy with another mapping"));
        break;
    case EACCES:
    case EPERM:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the system refused access to the loop driver"));
        break;
    case ENODEV:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the loop driver is not available"));
        break;
    case ENXIO:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the loop device went away"));
        break;
    case EINVAL:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the loop mapping was rejected"));
        break;
    case EIO:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the drive failed while writing"));
        break;
    case ENOSPC:
        reason = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "the loop mapping ran out of room"));
        break;
    default:
        reason = QString::fromLocal8Bit(std::strerror(errorNumber));
        break;
    }

    return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "%1: %2 (errno %3). %4."))
        .arg(operation, reason, errorNumber, advice);
}

namespace {
// The loop device is an internal detail; say what it was being used for and what
// the user can do about it, rather than naming a loop node as if it were the drive.
bool loopError(
    QString *error,
    int errorNumber,
    const RestoreMessage &operation,
    const RestoreMessage &advice,
    QString *displayError)
{
    setRestoreError(error, displayError, loopErrorMessage(operation, errorNumber, advice));
    return false;
}
} // namespace

LoopDevice::~LoopDevice()
{
    detach(nullptr);
}

bool LoopDevice::configure(
    int backingDescriptor,
    std::uint64_t offset,
    std::uint64_t length,
    quint32 sectorSize,
    QString *error,
    QString *displayError)
{
    if (m_fd >= 0 || m_claimFd >= 0) {
        setRestoreError(
            error,
            displayError,
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "A formatting area is already set up for this drive. Unplug the drive, plug it "
                "back in and try again.")));
        return false;
    }
    struct stat status = {};
    if (::fstat(backingDescriptor, &status) != 0) {
        const int errorNumber = errno;
        return loopError(
            error,
            errorNumber,
            restoreText(
                QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not read the drive through the formatting area")),
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Try the restore again")),
            displayError);
    }
    std::uint64_t size = 0;
    if (S_ISREG(status.st_mode)) {
        size = static_cast<std::uint64_t>(status.st_size);
    } else if (!S_ISBLK(status.st_mode) || ::ioctl(backingDescriptor, BLKGETSIZE64, &size) != 0) {
        setRestoreError(
            error,
            displayError,
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "The drive could not be read for formatting. Unplug it, plug it back in and try "
                "again.")));
        return false;
    }
    if (!isValidDeviceRange(size, offset, length, sectorSize)) {
        setRestoreError(
            error,
            displayError,
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "The space set aside for the new volume is outside the drive or is not aligned to "
                "its sectors. Refresh the list and select the drive again.")));
        return false;
    }
    const int control = ::open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (control < 0) {
        const int errorNumber = errno;
        return loopError(
            error,
            errorNumber,
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "Could not reach the Linux loop driver at /dev/loop-control, which formatting needs")),
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend", "Make sure the loop module is loaded (modprobe loop) and try again")),
            displayError);
    }
    bool busy = false;
    for (int attempt = 0; attempt < 8; ++attempt) {
        // GET_FREE returns the lowest unbound node, so after one is busy it would return the
        // same one again; a negative ADD allocates a new node instead.
        const int number = busy ? ::ioctl(control, LOOP_CTL_ADD, -1L) : ::ioctl(control, LOOP_CTL_GET_FREE);
        if (number < 0) {
            break;
        }
        m_path = QStringLiteral("/dev/loop%1").arg(number);
        m_claimFd = ::open(m_path.toLocal8Bit().constData(), O_RDWR | O_EXCL | O_CLOEXEC);
        if (m_claimFd < 0) {
            busy = errno == EBUSY;
            if (busy) {
                continue;
            }
            break;
        }
        struct loop_config config = {};
        config.fd = static_cast<quint32>(backingDescriptor);
        config.block_size = sectorSize;
        config.info.lo_offset = offset;
        config.info.lo_sizelimit = length;
        config.info.lo_flags = LO_FLAGS_AUTOCLEAR;
        if (::ioctl(m_claimFd, LOOP_CONFIGURE, &config) == 0) {
            m_fd = ::open(m_path.toLocal8Bit().constData(), O_RDWR | O_CLOEXEC);
            if (m_fd >= 0) {
                ::close(control);
                return true;
            }
            const int code = errno;
            ::ioctl(m_claimFd, LOOP_CLR_FD, 0);
            ::close(m_claimFd);
            m_claimFd = -1;
            errno = code;
            break;
        }
        const int code = errno;
        ::close(m_claimFd);
        m_claimFd = -1;
        errno = code;
        busy = code == EBUSY;
        if (!busy) {
            break;
        }
    }
    const int errorNumber = errno;
    ::close(control);
    m_path.clear();
    return loopError(
        error,
        errorNumber,
        restoreText(
            QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not set up the temporary area used to format the drive")),
        restoreText(QT_TRANSLATE_NOOP(
            "usbrestore::Backend",
            "Close other disk tools that may be holding loop devices, such as GNOME Disks, and try again")),
        displayError);
}

bool LoopDevice::prepareForFormatting(QString *error, QString *displayError)
{
    // The raw writer changed the backing sectors after this mapping was created.
    if (m_fd < 0 || ::ioctl(m_fd, BLKFLSBUF, 0) != 0) {
        const int errorNumber = errno;
        return loopError(
            error,
            errorNumber,
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend", "Could not make the new partition available to the formatter")),
            restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Unplug the drive, plug it back in and try again")),
            displayError);
    }
    releaseClaim();
    return true;
}

void LoopDevice::releaseClaim()
{
    if (m_claimFd >= 0) {
        ::close(m_claimFd);
        m_claimFd = -1;
    }
}

bool LoopDevice::detach(QString *error, QString *displayError)
{
    if (m_fd < 0) {
        releaseClaim();
        return true;
    }
    bool ok = true;
    if (::fsync(m_fd) != 0) {
        const int errorNumber = errno;
        ok = loopError(
            error,
            errorNumber,
            restoreText(
                QT_TRANSLATE_NOOP("usbrestore::Backend", "Could not finish writing the new volume to the drive")),
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "Unplug the drive, plug it back in and check whether the volume appears; if it does not, "
                "run the restore again")),
            displayError);
    }
    releaseClaim();
    if (::ioctl(m_fd, LOOP_CLR_FD, 0) != 0 && ok) {
        const int errorNumber = errno;
        ok = loopError(
            error,
            errorNumber,
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend",
                "The new volume was written, but its temporary formatting area could not be closed")),
            restoreText(QT_TRANSLATE_NOOP(
                "usbrestore::Backend", "Unplug the drive and plug it back in; this does not damage the drive")),
            displayError);
    }
    ::close(m_fd);
    m_fd = -1;
    m_path.clear();
    return ok;
}

} // namespace usbrestore

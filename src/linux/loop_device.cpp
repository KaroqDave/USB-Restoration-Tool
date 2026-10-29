#include "linux/loop_device.h"

#include "core/safety.h"

#include <fcntl.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace usbrestore {

// A loop-control or mapping failure is a kernel loop-driver problem, not a USB
// stick problem, except EIO, which comes from the drive underneath. The caller supplies
// the action, without a final full stop.
QString loopErrorText(const QString &operation, int errorNumber, const QString &advice)
{
    QString reason;
    switch (errorNumber) {
    case EBUSY:
        reason = QStringLiteral("the loop driver is busy with another mapping");
        break;
    case EACCES:
    case EPERM:
        reason = QStringLiteral("the system refused access to the loop driver");
        break;
    case ENODEV:
        reason = QStringLiteral("the loop driver is not available");
        break;
    case ENXIO:
        reason = QStringLiteral("the loop device went away");
        break;
    case EINVAL:
        reason = QStringLiteral("the loop mapping was rejected");
        break;
    case EIO:
        reason = QStringLiteral("the drive failed while writing");
        break;
    case ENOSPC:
        reason = QStringLiteral("the loop mapping ran out of room");
        break;
    default:
        reason = QString::fromLocal8Bit(std::strerror(errorNumber));
        break;
    }

    return QStringLiteral("%1: %2 (errno %3). %4.").arg(operation, reason).arg(errorNumber).arg(advice);
}

namespace {
// The loop device is an internal detail; say what it was being used for and what
// the user can do about it, rather than naming a loop node as if it were the drive.
bool loopError(QString *error, const QString &operation, const QString &advice)
{
    if (error) {
        *error = loopErrorText(operation, errno, advice);
    }
    return false;
}
} // namespace

LoopDevice::~LoopDevice()
{
    detach(nullptr);
}

bool LoopDevice::configure(
    int backingDescriptor, std::uint64_t offset, std::uint64_t length, quint32 sectorSize, QString *error)
{
    if (m_fd >= 0 || m_claimFd >= 0) {
        if (error) {
            *error = QStringLiteral(
                "A formatting area is already set up for this drive. Unplug the drive, plug it "
                "back in and try again.");
        }
        return false;
    }
    struct stat status = {};
    if (::fstat(backingDescriptor, &status) != 0) {
        return loopError(
            error,
            QStringLiteral("Could not read the drive through the formatting area"),
            QStringLiteral("Try the restore again"));
    }
    std::uint64_t size = 0;
    if (S_ISREG(status.st_mode)) {
        size = static_cast<std::uint64_t>(status.st_size);
    } else if (!S_ISBLK(status.st_mode) || ::ioctl(backingDescriptor, BLKGETSIZE64, &size) != 0) {
        if (error) {
            *error = QStringLiteral(
                "The drive could not be read for formatting. Unplug it, plug it back in and try "
                "again.");
        }
        return false;
    }
    if (!isValidDeviceRange(size, offset, length, sectorSize)) {
        if (error) {
            *error = QStringLiteral(
                "The space set aside for the new volume is outside the drive or is not aligned to "
                "its sectors. Refresh the list and select the drive again.");
        }
        return false;
    }
    const int control = ::open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (control < 0) {
        return loopError(
            error,
            QStringLiteral("Could not reach the Linux loop driver at /dev/loop-control, which formatting needs"),
            QStringLiteral("Make sure the loop module is loaded (modprobe loop) and try again"));
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
    const int code = errno;
    ::close(control);
    m_path.clear();
    errno = code;
    return loopError(
        error,
        QStringLiteral("Could not set up the temporary area used to format the drive"),
        QStringLiteral("Close other disk tools that may be holding loop devices, such as GNOME Disks, and try again"));
}

bool LoopDevice::prepareForFormatting(QString *error)
{
    // The raw writer changed the backing sectors after this mapping was created.
    if (m_fd < 0 || ::ioctl(m_fd, BLKFLSBUF, 0) != 0) {
        return loopError(
            error,
            QStringLiteral("Could not make the new partition available to the formatter"),
            QStringLiteral("Unplug the drive, plug it back in and try again"));
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

bool LoopDevice::detach(QString *error)
{
    if (m_fd < 0) {
        releaseClaim();
        return true;
    }
    bool ok = true;
    if (::fsync(m_fd) != 0) {
        ok = loopError(
            error,
            QStringLiteral("Could not finish writing the new volume to the drive"),
            QStringLiteral(
                "Unplug the drive, plug it back in and check whether the volume appears; if it does not, "
                "run the restore again"));
    }
    releaseClaim();
    if (::ioctl(m_fd, LOOP_CLR_FD, 0) != 0 && ok) {
        ok = loopError(
            error,
            QStringLiteral("The new volume was written, but its temporary formatting area could not be closed"),
            QStringLiteral("Unplug the drive and plug it back in; this does not damage the drive"));
    }
    ::close(m_fd);
    m_fd = -1;
    m_path.clear();
    return ok;
}

} // namespace usbrestore

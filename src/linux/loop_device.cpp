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

namespace {
bool loopError(QString *error, const QString &operation)
{
    const int code = errno;
    if (error) {
        *error = QStringLiteral("%1: %2 (errno %3). Check that the Linux loop driver is available and try again.")
                     .arg(operation, QString::fromLocal8Bit(std::strerror(code)))
                     .arg(code);
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
            *error = QStringLiteral("The formatter mapping is already in use.");
        }
        return false;
    }
    struct stat status = {};
    if (::fstat(backingDescriptor, &status) != 0) {
        return loopError(error, QStringLiteral("Could not inspect the formatter's backing file"));
    }
    std::uint64_t size = 0;
    if (S_ISREG(status.st_mode)) {
        size = static_cast<std::uint64_t>(status.st_size);
    } else if (!S_ISBLK(status.st_mode) || ::ioctl(backingDescriptor, BLKGETSIZE64, &size) != 0) {
        if (error) {
            *error = QStringLiteral("The formatter backing must be a readable disk or image file.");
        }
        return false;
    }
    if (!isValidDeviceRange(size, offset, length, sectorSize)) {
        if (error) {
            *error = QStringLiteral("The formatter mapping is outside the disk or is not sector-aligned.");
        }
        return false;
    }
    const int control = ::open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    if (control < 0) {
        return loopError(error, QStringLiteral("Could not open /dev/loop-control"));
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
    return loopError(error, QStringLiteral("Could not create a pinned formatter mapping"));
}

bool LoopDevice::prepareForFormatting(QString *error)
{
    // The raw writer changed the backing sectors after this mapping was created.
    if (m_fd < 0 || ::ioctl(m_fd, BLKFLSBUF, 0) != 0) {
        return loopError(error, QStringLiteral("Could not invalidate the formatter mapping's cache"));
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
        ok = loopError(error, QStringLiteral("Could not flush the formatted volume"));
    }
    releaseClaim();
    if (::ioctl(m_fd, LOOP_CLR_FD, 0) != 0 && ok) {
        ok = loopError(error, QStringLiteral("Could not release the formatter mapping"));
    }
    ::close(m_fd);
    m_fd = -1;
    m_path.clear();
    return ok;
}

} // namespace usbrestore

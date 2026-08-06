#include "win/raw_disk.h"

#include "core/safety.h"
#include "win/windows_util.h"

#include <QByteArray>
#include <QtGlobal>

#include <objbase.h>
#include <winioctl.h>

namespace usbrestore {

namespace {

// GPT Basic Data Partition: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
constexpr GUID BasicDataPartitionGuid = {0xebd0a0a2,
                                         0xb9e5,
                                         0x4433,
                                         {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}};

// The window at each end of the disk that gets zeroed. A GPT header lives in
// the first and last 33 sectors, an MBR in the first, and hybrid ISO layouts
// scatter signatures through the first megabyte; 4 MiB covers all of them with
// room to spare at both ends.
constexpr std::uint64_t SignatureClearBytes = 4ull * 1024ull * 1024ull;

std::uint64_t alignDown(std::uint64_t value, std::uint64_t alignment)
{
    return alignment == 0 ? value : (value / alignment) * alignment;
}

// The serial number lives at a byte offset the driver reports, inside the same
// buffer. Both the offset and the string's length are therefore driver-supplied
// and are bounded against the bytes that actually came back.
QString serialFromDescriptor(const QByteArray &buffer)
{
    if (buffer.size() < static_cast<int>(sizeof(STORAGE_DEVICE_DESCRIPTOR))) {
        return {};
    }
    const auto *descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR *>(buffer.constData());
    const DWORD offset = descriptor->SerialNumberOffset;
    if (offset == 0 || offset >= static_cast<DWORD>(buffer.size())) {
        return {};
    }

    const char *start = buffer.constData() + offset;
    const qsizetype available = buffer.size() - static_cast<qsizetype>(offset);
    qsizetype length = 0;
    while (length < available && start[length] != '\0') {
        ++length;
    }
    return QString::fromLatin1(start, length).trimmed();
}

QString normalizedSerial(const QString &value)
{
    QString normalized;
    normalized.reserve(value.size());
    for (const QChar character : value) {
        if (character.isLetterOrNumber()) {
            normalized.append(character.toUpper());
        }
    }
    return normalized;
}

// The same device's serial reaches WMI and the storage descriptor by different
// routes, and the two can differ in padding, punctuation, or a vendor prefix on
// one side only. So the comparison is deliberately loose: it exists to catch a
// different physical device, not to insist on identical formatting. Either side
// being silent about its serial proves nothing and is not treated as a
// mismatch — the device number, bus type, byte length and sector size checks
// around it do not depend on this one.
bool serialsAgree(const QString &reported, const QString &expected)
{
    const QString a = normalizedSerial(reported);
    const QString b = normalizedSerial(expected);
    if (a.isEmpty() || b.isEmpty()) {
        return true;
    }
    return a == b || a.contains(b) || b.contains(a);
}

// Two-step read: the header says how big the descriptor is, then it is read in
// full. A single fixed-size buffer would fail outright on a device with a long
// vendor or product string rather than returning what it asked for.
bool readStorageDeviceDescriptor(HANDLE handle, QByteArray *buffer)
{
    STORAGE_PROPERTY_QUERY query = {};
    query.PropertyId = StorageDeviceProperty;
    query.QueryType = PropertyStandardQuery;

    STORAGE_DESCRIPTOR_HEADER header = {};
    DWORD returned = 0;
    if (!DeviceIoControl(handle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &query,
                         sizeof(query),
                         &header,
                         sizeof(header),
                         &returned,
                         nullptr)) {
        return false;
    }
    if (returned < sizeof(header) || header.Size < sizeof(STORAGE_DEVICE_DESCRIPTOR) ||
        header.Size > 64 * 1024) {
        return false;
    }

    QByteArray full(static_cast<int>(header.Size), '\0');
    returned = 0;
    if (!DeviceIoControl(handle,
                         IOCTL_STORAGE_QUERY_PROPERTY,
                         &query,
                         sizeof(query),
                         full.data(),
                         static_cast<DWORD>(full.size()),
                         &returned,
                         nullptr)) {
        return false;
    }
    if (returned < sizeof(STORAGE_DEVICE_DESCRIPTOR)) {
        return false;
    }

    full.resize(static_cast<int>(returned));
    *buffer = full;
    return true;
}

} // namespace

RawDisk::RawDisk(quint32 diskNumber) : m_diskNumber(diskNumber) {}

RawDisk::~RawDisk()
{
    if (m_handle != INVALID_HANDLE_VALUE) {
        if (m_locked) {
            DWORD unused = 0;
            DeviceIoControl(m_handle, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr);
        }
        CloseHandle(m_handle);
    }
}

bool RawDisk::open(QString *error)
{
    const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(m_diskNumber);
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()),
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = lastError(QStringLiteral("Could not open %1").arg(path));
        }
        return false;
    }
    return true;
}

bool RawDisk::verifyIdentity(const DiskInfo &expected, QString *error)
{
    const auto refuse = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return false;
    };

    if (m_handle == INVALID_HANDLE_VALUE) {
        return refuse(QStringLiteral("The physical drive is not open."));
    }

    DWORD returned = 0;

    STORAGE_DEVICE_NUMBER deviceNumber = {};
    if (!DeviceIoControl(m_handle,
                         IOCTL_STORAGE_GET_DEVICE_NUMBER,
                         nullptr,
                         0,
                         &deviceNumber,
                         sizeof(deviceNumber),
                         &returned,
                         nullptr)) {
        return refuse(lastError(QStringLiteral("Could not read the device number of the opened drive")));
    }
    if (deviceNumber.DeviceType != FILE_DEVICE_DISK) {
        return refuse(QStringLiteral("The opened device is not a disk."));
    }
    if (deviceNumber.DeviceNumber != m_diskNumber || deviceNumber.PartitionNumber > 0) {
        return refuse(QStringLiteral("The opened handle points at disk %1, not the whole of disk %2.")
                          .arg(deviceNumber.DeviceNumber)
                          .arg(m_diskNumber));
    }

    // Bus type is re-read from the device rather than trusted from the WMI
    // snapshot taken earlier: this is the check that a replug between
    // enumeration and here cannot slip past.
    QByteArray descriptorBuffer;
    if (!readStorageDeviceDescriptor(m_handle, &descriptorBuffer)) {
        return refuse(lastError(QStringLiteral("Could not read the storage properties of the opened drive")));
    }

    const auto *descriptor = reinterpret_cast<const STORAGE_DEVICE_DESCRIPTOR *>(descriptorBuffer.constData());
    if (descriptor->BusType != BusTypeUsb) {
        return refuse(QStringLiteral("Disk %1 is not on the USB bus.").arg(m_diskNumber));
    }

    if (!serialsAgree(serialFromDescriptor(descriptorBuffer), expected.serialNumber)) {
        return refuse(QStringLiteral("Disk %1 reports a different serial number than the disk that was selected.")
                          .arg(m_diskNumber));
    }

    GET_LENGTH_INFORMATION length = {};
    if (!DeviceIoControl(
            m_handle, IOCTL_DISK_GET_LENGTH_INFO, nullptr, 0, &length, sizeof(length), &returned, nullptr)) {
        return refuse(lastError(QStringLiteral("Could not read the length of the opened drive")));
    }
    const auto reportedSize = static_cast<std::uint64_t>(length.Length.QuadPart);
    if (expected.size != 0 && reportedSize != expected.size) {
        return refuse(QStringLiteral("Disk %1 now reports %2 instead of the %3 that was selected.")
                          .arg(m_diskNumber)
                          .arg(formatByteSize(reportedSize), formatByteSize(expected.size)));
    }

    DISK_GEOMETRY_EX geometry = {};
    if (DeviceIoControl(m_handle,
                        IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
                        nullptr,
                        0,
                        &geometry,
                        sizeof(geometry),
                        &returned,
                        nullptr)) {
        const auto reportedSectorSize = static_cast<quint32>(geometry.Geometry.BytesPerSector);
        if (reportedSectorSize != 0 && expected.sectorSize != 0 && reportedSectorSize != expected.sectorSize) {
            return refuse(QStringLiteral("Disk %1 reports %2-byte sectors instead of the %3-byte sectors that "
                                         "were selected.")
                              .arg(m_diskNumber)
                              .arg(reportedSectorSize)
                              .arg(expected.sectorSize));
        }
    }

    if (!DeviceIoControl(m_handle, IOCTL_DISK_IS_WRITABLE, nullptr, 0, nullptr, 0, &returned, nullptr)) {
        const DWORD code = GetLastError();
        if (code == ERROR_WRITE_PROTECT) {
            return refuse(QStringLiteral("Disk %1 is write-protected.").arg(m_diskNumber));
        }
        return refuse(windowsErrorMessage(QStringLiteral("Disk %1 did not report itself as writable")
                                              .arg(m_diskNumber),
                                          code));
    }

    return true;
}

bool RawDisk::lock(QString *error)
{
    DWORD unused = 0;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (DeviceIoControl(m_handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr)) {
            m_locked = true;
            return true;
        }
        Sleep(500);
    }
    if (error) {
        *error = lastError(QStringLiteral("Could not lock the physical drive"));
    }
    return false;
}

bool RawDisk::allowExtendedIo(QString *error)
{
    DWORD unused = 0;
    if (!DeviceIoControl(m_handle, FSCTL_ALLOW_EXTENDED_DASD_IO, nullptr, 0, nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not enable extended disk I/O"));
        }
        return false;
    }
    return true;
}

bool RawDisk::refreshLayout(QString *error)
{
    DWORD unused = 0;
    if (!DeviceIoControl(m_handle, IOCTL_DISK_UPDATE_PROPERTIES, nullptr, 0, nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not refresh disk properties"));
        }
        return false;
    }
    return true;
}

bool RawDisk::clearPartitionSignatures(std::uint64_t diskSize, quint32 sectorSize, QString *error)
{
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;

    // Never let the head and tail windows meet in the middle: on a small disk
    // a fixed 4 MiB from each end would overlap and the second write would
    // seek backwards past the first.
    const std::uint64_t clearBytes = alignDown(qMin(SignatureClearBytes, diskSize / 4), sector);
    if (clearBytes < sector) {
        if (error) {
            *error = QStringLiteral("Disk is too small to clear safely.");
        }
        return false;
    }

    if (!writeZeros(0, clearBytes, sector, error)) {
        return false;
    }
    if (!writeZeros(alignDown(diskSize - clearBytes, sector), clearBytes, sector, error)) {
        return false;
    }

    FlushFileBuffers(m_handle);
    refreshLayout(nullptr);
    return true;
}

bool RawDisk::setRaw(QString *error)
{
    CREATE_DISK createDisk = {};
    createDisk.PartitionStyle = PARTITION_STYLE_RAW;
    DWORD unused = 0;
    if (!DeviceIoControl(
            m_handle, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not set the disk to RAW"));
        }
        return false;
    }
    refreshLayout(nullptr);
    return true;
}

bool RawDisk::createSingleGptPartition(std::uint64_t diskSize, quint32 sectorSize, QString *error)
{
    const GptLayout layout = calculateGptLayout(diskSize, sectorSize);
    if (layout.length == 0) {
        if (error) {
            *error = QStringLiteral("Disk is too small for a safe GPT layout.");
        }
        return false;
    }

    GUID diskId = {};
    if (FAILED(CoCreateGuid(&diskId))) {
        if (error) {
            *error = QStringLiteral("Could not generate a disk identifier.");
        }
        return false;
    }

    CREATE_DISK createDisk = {};
    createDisk.PartitionStyle = PARTITION_STYLE_GPT;
    createDisk.Gpt.DiskId = diskId;

    DWORD unused = 0;
    if (!DeviceIoControl(
            m_handle, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not create the GPT disk"));
        }
        return false;
    }
    if (!refreshLayout(error)) {
        return false;
    }

    DRIVE_LAYOUT_INFORMATION_EX driveLayout = {};
    driveLayout.PartitionStyle = PARTITION_STYLE_GPT;
    driveLayout.PartitionCount = 1;
    driveLayout.Gpt.DiskId = diskId;
    driveLayout.Gpt.StartingUsableOffset.QuadPart = static_cast<LONGLONG>(layout.startOffset);
    driveLayout.Gpt.UsableLength.QuadPart = static_cast<LONGLONG>(layout.length);
    driveLayout.Gpt.MaxPartitionCount = 128;

    PARTITION_INFORMATION_EX &partition = driveLayout.PartitionEntry[0];
    partition.PartitionStyle = PARTITION_STYLE_GPT;
    partition.StartingOffset.QuadPart = static_cast<LONGLONG>(layout.startOffset);
    partition.PartitionLength.QuadPart = static_cast<LONGLONG>(layout.length);
    partition.PartitionNumber = 1;
    partition.RewritePartition = TRUE;
    partition.Gpt.PartitionType = BasicDataPartitionGuid;
    partition.Gpt.Attributes = 0;
    if (FAILED(CoCreateGuid(&partition.Gpt.PartitionId))) {
        if (error) {
            *error = QStringLiteral("Could not generate a partition identifier.");
        }
        return false;
    }
    wcscpy_s(partition.Gpt.Name, L"USB");

    if (!DeviceIoControl(m_handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         &driveLayout,
                         sizeof(driveLayout),
                         nullptr,
                         0,
                         &unused,
                         nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not create the partition layout"));
        }
        return false;
    }

    return refreshLayout(error);
}

bool RawDisk::writeZeros(std::uint64_t offset, std::uint64_t bytes, quint32 sectorSize, QString *error)
{
    if (bytes == 0) {
        return true;
    }

    LARGE_INTEGER pointer;
    pointer.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(m_handle, pointer, nullptr, FILE_BEGIN)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not seek on the disk"));
        }
        return false;
    }

    // Raw writes to a physical drive have to be whole sectors, so the buffer is
    // a sector multiple rather than a round megabyte.
    const std::uint64_t sector = sectorSize == 0 ? 512 : sectorSize;
    const auto bufferSize = static_cast<int>(alignDown(1024ull * 1024ull, sector));
    QByteArray buffer(bufferSize > 0 ? bufferSize : static_cast<int>(sector), '\0');

    std::uint64_t remaining = bytes;
    while (remaining > 0) {
        const auto toWrite =
            static_cast<DWORD>(qMin<std::uint64_t>(remaining, static_cast<std::uint64_t>(buffer.size())));
        DWORD written = 0;
        if (!WriteFile(m_handle, buffer.constData(), toWrite, &written, nullptr)) {
            if (error) {
                *error = lastError(QStringLiteral("Could not write zeroes to the disk"));
            }
            return false;
        }
        if (written == 0) {
            if (error) {
                *error = QStringLiteral("The disk stopped accepting writes before the area was cleared.");
            }
            return false;
        }
        remaining -= written;
    }
    return true;
}

QString RawDisk::lastError(const QString &context) const
{
    return windowsErrorMessage(context, GetLastError());
}

} // namespace usbrestore

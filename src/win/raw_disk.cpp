#include "win/raw_disk.h"

#include "core/partition_table.h"
#include "core/safety.h"
#include "win/windows_util.h"

#include <QByteArray>
#include <QtGlobal>

#include <objbase.h>
#include <winioctl.h>

#include <cstddef>

namespace usbrestore {

namespace {

// GPT Basic Data Partition: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
constexpr GUID BasicDataPartitionGuid = {0xebd0a0a2,
                                         0xb9e5,
                                         0x4433,
                                         {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}};

// Zeroed at each end: covers GPT (first/last 33 sectors), MBR and hybrid-ISO
// signatures in the first megabyte, with room to spare.
constexpr std::uint64_t SignatureClearBytes = 4ull * 1024ull * 1024ull;

std::uint64_t alignDown(std::uint64_t value, std::uint64_t alignment)
{
    return alignment == 0 ? value : (value / alignment) * alignment;
}

// The offset and string length are driver-supplied, so both are bounded by the returned bytes.
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

// Deliberately loose: WMI and the storage descriptor can differ in padding, punctuation or a
// vendor prefix. A missing serial on either side proves nothing, so it is not a mismatch.
bool serialsAgree(const QString &reported, const QString &expected)
{
    const QString a = normalizedSerial(reported);
    const QString b = normalizedSerial(expected);
    if (a.isEmpty() || b.isEmpty()) {
        return true;
    }
    return a == b || a.contains(b) || b.contains(a);
}

// Read the header first for the size: a fixed buffer fails outright on long vendor/product strings.
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

    // Re-read from the device, not the WMI snapshot, so a replug since enumeration is caught.
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

    // On a small disk a fixed 4 MiB from each end would overlap.
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

bool RawDisk::replaceLayoutWithEmptyGpt(QString *error)
{
    // A RAW or MBR reset keeps the old boot code, and with its jump instruction Windows can take the
    // disk for a superfloppy again; the GPT's protective MBR replaces it.
    GUID diskId = {};
    if (!createGptDisk(&diskId, error)) {
        return false;
    }

    QString readError;
    for (int attempt = 0; attempt < 20; ++attempt) {
        refreshLayout(nullptr);
        bool hasPartitions = true;
        readError.clear();
        if (layoutHasPartitions(&hasPartitions, &readError) && !hasPartitions) {
            return true;
        }
        Sleep(250);
    }
    if (error) {
        const QString reason =
            readError.isEmpty()
                ? QStringLiteral("Windows still lists a partition on disk %1 after an empty partition table was "
                                 "written.")
                      .arg(m_diskNumber)
                : readError + QLatin1Char('.');
        *error = QStringLiteral("%1 Unplug the USB disk, plug it back in and run the restore again.").arg(reason);
    }
    return false;
}

bool RawDisk::createGptDisk(GUID *diskId, QString *error)
{
    if (FAILED(CoCreateGuid(diskId))) {
        if (error) {
            *error = QStringLiteral("Could not generate an identifier for a partition table on disk %1. Run the "
                                    "restore again.")
                         .arg(m_diskNumber);
        }
        return false;
    }

    CREATE_DISK createDisk = {};
    createDisk.PartitionStyle = PARTITION_STYLE_GPT;
    createDisk.Gpt.DiskId = *diskId;
    DWORD unused = 0;
    if (!DeviceIoControl(
            m_handle, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), nullptr, 0, &unused, nullptr)) {
        const DWORD code = GetLastError();
        if (error) {
            *error =
                QStringLiteral("%1. Check that the USB disk is not write-protected, plug it back in and run the "
                               "restore again.")
                    .arg(windowsErrorMessage(
                        QStringLiteral("Could not write a GPT partition table to disk %1").arg(m_diskNumber), code));
        }
        return false;
    }
    return true;
}

// Fails rather than guessing when the layout cannot be read.
bool RawDisk::layoutHasPartitions(bool *hasPartitions, QString *error) const
{
    constexpr int MaxEntries = 128;
    const int entriesOffset = static_cast<int>(offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry));
    QByteArray buffer(entriesOffset + static_cast<int>(sizeof(PARTITION_INFORMATION_EX)) * MaxEntries, '\0');
    DWORD returned = 0;
    if (!DeviceIoControl(
            m_handle,
            IOCTL_DISK_GET_DRIVE_LAYOUT_EX,
            nullptr,
            0,
            buffer.data(),
            static_cast<DWORD>(buffer.size()),
            &returned,
            nullptr)) {
        const DWORD code = GetLastError();
        if (error) {
            *error = windowsErrorMessage(
                QStringLiteral("Could not read the partition table of disk %1").arg(m_diskNumber), code);
        }
        return false;
    }
    if (returned < static_cast<DWORD>(entriesOffset)) {
        if (error) {
            *error = QStringLiteral("Windows returned an incomplete partition table for disk %1").arg(m_diskNumber);
        }
        return false;
    }

    const auto *layout = reinterpret_cast<const DRIVE_LAYOUT_INFORMATION_EX *>(buffer.constData());
    const std::uint32_t count =
        boundedEntryCount(layout->PartitionCount, returned, entriesOffset, sizeof(PARTITION_INFORMATION_EX));
    *hasPartitions = false;
    for (std::uint32_t index = 0; index < count; ++index) {
        if (layout->PartitionEntry[index].PartitionLength.QuadPart > 0) {
            *hasPartitions = true;
            break;
        }
    }
    return true;
}

bool RawDisk::createSinglePartition(PartitionStyle style,
                                    FileSystemType fileSystem,
                                    std::uint64_t diskSize,
                                    quint32 sectorSize,
                                    QString *error)
{
    const GptLayout layout = calculateGptLayout(diskSize, sectorSize);
    if (layout.length == 0) {
        if (error) {
            *error = QStringLiteral("Disk is too small for a safe partition layout.");
        }
        return false;
    }

    if (style == PartitionStyle::Mbr) {
        return createSingleMbrPartition(layout, fileSystem, sectorSize, error);
    }

    GUID diskId = {};
    if (!createGptDisk(&diskId, error) || !refreshLayout(error)) {
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

    DWORD unused = 0;
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

bool RawDisk::createSingleMbrPartition(const GptLayout &layout, FileSystemType fileSystem, quint32 sectorSize,
                                       QString *error)
{
    const std::uint64_t sector = isSupportedSectorSize(sectorSize) ? sectorSize : 512;
    const std::uint64_t endSector = (layout.startOffset + layout.length) / sector;
    if (endSector > 0xFFFFFFFFull) {
        if (error) {
            *error = QStringLiteral("MBR cannot address a partition this far into the disk. Use GPT instead.");
        }
        return false;
    }

    GUID signatureSource = {};
    if (FAILED(CoCreateGuid(&signatureSource))) {
        if (error) {
            *error = QStringLiteral("Could not generate a disk signature.");
        }
        return false;
    }

    CREATE_DISK createDisk = {};
    createDisk.PartitionStyle = PARTITION_STYLE_MBR;
    createDisk.Mbr.Signature = signatureSource.Data1;

    DWORD unused = 0;
    if (!DeviceIoControl(
            m_handle, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not create the MBR disk"));
        }
        return false;
    }
    if (!refreshLayout(error)) {
        return false;
    }

    // IOCTL_DISK_SET_DRIVE_LAYOUT_EX rejects any MBR count but four; the unused
    // slots are rewritten empty so nothing of the old table survives.
    constexpr int MbrPartitionCount = 4;
    const int layoutBytes =
        static_cast<int>(offsetof(DRIVE_LAYOUT_INFORMATION_EX, PartitionEntry)) +
        static_cast<int>(sizeof(PARTITION_INFORMATION_EX)) * MbrPartitionCount;
    QByteArray buffer(layoutBytes, '\0');

    auto *driveLayout = reinterpret_cast<DRIVE_LAYOUT_INFORMATION_EX *>(buffer.data());
    driveLayout->PartitionStyle = PARTITION_STYLE_MBR;
    driveLayout->PartitionCount = MbrPartitionCount;
    driveLayout->Mbr.Signature = signatureSource.Data1;

    PARTITION_INFORMATION_EX &partition = driveLayout->PartitionEntry[0];
    partition.PartitionStyle = PARTITION_STYLE_MBR;
    partition.StartingOffset.QuadPart = static_cast<LONGLONG>(layout.startOffset);
    partition.PartitionLength.QuadPart = static_cast<LONGLONG>(layout.length);
    partition.PartitionNumber = 1;
    partition.RewritePartition = TRUE;
    // 0x07 for exFAT/NTFS, 0x0C for FAT32 LBA; older devices check this to recognise the stick.
    partition.Mbr.PartitionType = mbrPartitionType(fileSystem);
    partition.Mbr.BootIndicator = FALSE;
    partition.Mbr.RecognizedPartition = TRUE;
    partition.Mbr.HiddenSectors = static_cast<DWORD>(layout.startOffset / sector);

    for (int i = 1; i < MbrPartitionCount; ++i) {
        driveLayout->PartitionEntry[i].PartitionStyle = PARTITION_STYLE_MBR;
        driveLayout->PartitionEntry[i].RewritePartition = TRUE;
    }

    if (!DeviceIoControl(m_handle,
                         IOCTL_DISK_SET_DRIVE_LAYOUT_EX,
                         buffer.data(),
                         static_cast<DWORD>(buffer.size()),
                         nullptr,
                         0,
                         &unused,
                         nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not create the MBR partition layout"));
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

    const QString unaligned = unalignedZeroRangeError(offset, bytes, sectorSize);
    if (!unaligned.isEmpty()) {
        if (error) {
            *error = unaligned;
        }
        return false;
    }

    LARGE_INTEGER pointer;
    pointer.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(m_handle, pointer, nullptr, FILE_BEGIN)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not seek on the disk"));
        }
        return false;
    }

    // Raw physical-drive writes must be whole sectors.
    const std::uint64_t sector = sectorSize == 0 ? 512 : sectorSize;
    const auto bufferSize = static_cast<int>(alignDown(1024ull * 1024ull, sector));
    QByteArray buffer(bufferSize > 0 ? bufferSize : static_cast<int>(sector), '\0');

    std::uint64_t remaining = bytes;
    int notReadyRetries = 0;
    while (remaining > 0) {
        const auto toWrite =
            static_cast<DWORD>(qMin<std::uint64_t>(remaining, static_cast<std::uint64_t>(buffer.size())));
        DWORD written = 0;
        if (!WriteFile(m_handle, buffer.constData(), toWrite, &written, nullptr)) {
            const DWORD code = GetLastError();
            // Until a write lands, NOT_READY can be a volume Windows is still removing after the layout
            // was replaced; once one has, it is the device, and waiting would only delay the error.
            if (code == ERROR_NOT_READY && !m_writeAccepted && notReadyRetries < 10) {
                ++notReadyRetries;
                Sleep(500);
                pointer.QuadPart = static_cast<LONGLONG>(offset + (bytes - remaining));
                if (!SetFilePointerEx(m_handle, pointer, nullptr, FILE_BEGIN)) {
                    if (error) {
                        *error = lastError(QStringLiteral("Could not seek on the disk"));
                    }
                    return false;
                }
                continue;
            }
            if (error) {
                *error = windowsErrorMessage(QStringLiteral("Could not write zeroes to the disk"), code);
            }
            return false;
        }
        if (written == 0) {
            if (error) {
                *error = QStringLiteral("The disk stopped accepting writes before the area was cleared.");
            }
            return false;
        }
        m_writeAccepted = true;
        remaining -= written;
    }
    return true;
}

QString RawDisk::lastError(const QString &context) const
{
    return windowsErrorMessage(context, GetLastError());
}

} // namespace usbrestore

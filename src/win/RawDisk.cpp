#include "win/RawDisk.h"

#include <QByteArray>
#include <QtGlobal>
#include <objbase.h>
#include <winioctl.h>

namespace usbrestore {

namespace {

// GPT Basic Data Partition: EBD0A0A2-B9E5-4433-87C0-68B6B72699C7
constexpr GUID BasicDataPartitionGuid = {
    0xebd0a0a2,
    0xb9e5,
    0x4433,
    {0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7}
};

}

RawDisk::RawDisk(quint32 diskNumber)
    : m_diskNumber(diskNumber)
{
}

RawDisk::~RawDisk()
{
    if (m_handle != INVALID_HANDLE_VALUE) {
        DWORD unused = 0;
        DeviceIoControl(m_handle, FSCTL_UNLOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr);
        CloseHandle(m_handle);
    }
}

bool RawDisk::open(QString *error)
{
    const QString path = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(m_diskNumber);
    m_handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
    if (m_handle == INVALID_HANDLE_VALUE) {
        if (error) {
            *error = lastError(QStringLiteral("Could not open %1").arg(path));
        }
        return false;
    }
    return true;
}

bool RawDisk::lock(QString *error)
{
    DWORD unused = 0;
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (DeviceIoControl(m_handle, FSCTL_LOCK_VOLUME, nullptr, 0, nullptr, 0, &unused, nullptr)) {
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
    const std::uint64_t sector = sectorSize == 0 ? 512 : sectorSize;
    const std::uint64_t clearBytes = qMin<std::uint64_t>(4ull * 1024ull * 1024ull, (diskSize / 2 / sector) * sector);
    if (clearBytes < sector) {
        if (error) {
            *error = QStringLiteral("Disk is too small to clear safely.");
        }
        return false;
    }

    if (!writeZeros(0, clearBytes, error)) {
        return false;
    }
    if (!writeZeros(diskSize - clearBytes, clearBytes, error)) {
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
    if (!DeviceIoControl(m_handle, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), nullptr, 0, &unused, nullptr)) {
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
    GUID diskId = {};
    CoCreateGuid(&diskId);

    CREATE_DISK createDisk = {};
    createDisk.PartitionStyle = PARTITION_STYLE_GPT;
    createDisk.Gpt.DiskId = diskId;

    DWORD unused = 0;
    if (!DeviceIoControl(m_handle, IOCTL_DISK_CREATE_DISK, &createDisk, sizeof(createDisk), nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not create the GPT disk"));
        }
        return false;
    }
    if (!refreshLayout(error)) {
        return false;
    }

    const GptLayout layout = calculateGptLayout(diskSize, sectorSize);
    if (layout.length == 0) {
        if (error) {
            *error = QStringLiteral("Disk is too small for a safe GPT layout.");
        }
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
    CoCreateGuid(&partition.Gpt.PartitionId);
    wcscpy_s(partition.Gpt.Name, L"USB");

    if (!DeviceIoControl(m_handle, IOCTL_DISK_SET_DRIVE_LAYOUT_EX, &driveLayout, sizeof(driveLayout), nullptr, 0, &unused, nullptr)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not create the partition layout"));
        }
        return false;
    }

    return refreshLayout(error);
}

bool RawDisk::writeZeros(std::uint64_t offset, std::uint64_t bytes, QString *error)
{
    LARGE_INTEGER pointer;
    pointer.QuadPart = static_cast<LONGLONG>(offset);
    if (!SetFilePointerEx(m_handle, pointer, nullptr, FILE_BEGIN)) {
        if (error) {
            *error = lastError(QStringLiteral("Could not seek on the disk"));
        }
        return false;
    }

    QByteArray buffer(1024 * 1024, '\0');
    std::uint64_t remaining = bytes;
    while (remaining > 0) {
        const DWORD toWrite = static_cast<DWORD>(qMin<std::uint64_t>(remaining, static_cast<std::uint64_t>(buffer.size())));
        DWORD written = 0;
        if (!WriteFile(m_handle, buffer.constData(), toWrite, &written, nullptr) || written != toWrite) {
            if (error) {
                *error = lastError(QStringLiteral("Could not write zeroes to the disk"));
            }
            return false;
        }
        remaining -= written;
    }
    return true;
}

QString RawDisk::lastError(const QString &context) const
{
    const DWORD code = GetLastError();
    wchar_t *buffer = nullptr;
    FormatMessageW(FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   nullptr, code, 0, reinterpret_cast<LPWSTR>(&buffer), 0, nullptr);
    const QString detail = buffer ? QString::fromWCharArray(buffer).trimmed() : QStringLiteral("Windows error %1").arg(code);
    if (buffer) {
        LocalFree(buffer);
    }
    return QStringLiteral("%1: %2").arg(context, detail);
}

}

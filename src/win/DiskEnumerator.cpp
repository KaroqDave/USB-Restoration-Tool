#include "win/DiskEnumerator.h"

#include "win/Wmi.h"

#include <QByteArray>
#include <Windows.h>
#include <winioctl.h>

namespace usbrestore {

namespace {

bool volumeHasDiskExtent(HANDLE handle, quint32 diskNumber)
{
    DWORD bufferSize = sizeof(VOLUME_DISK_EXTENTS);
    QByteArray buffer(static_cast<int>(bufferSize), '\0');

    for (int attempt = 0; attempt < 5; ++attempt) {
        DWORD returned = 0;
        const BOOL ok = DeviceIoControl(handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, nullptr, 0,
                                        buffer.data(), static_cast<DWORD>(buffer.size()), &returned, nullptr);
        if (ok) {
            const auto *extents = reinterpret_cast<const VOLUME_DISK_EXTENTS *>(buffer.constData());
            for (DWORD i = 0; i < extents->NumberOfDiskExtents; ++i) {
                if (extents->Extents[i].DiskNumber == diskNumber) {
                    return true;
                }
            }
            return false;
        }

        if (GetLastError() != ERROR_MORE_DATA) {
            return false;
        }

        bufferSize = qMax<DWORD>(returned, bufferSize + sizeof(DISK_EXTENT) * 16);
        buffer.resize(static_cast<int>(bufferSize));
    }

    return false;
}

}

QVector<DiskInfo> DiskEnumerator::listUsbDisks(QString *error) const
{
    QVector<DiskInfo> disks;
    WmiConnection storage(L"ROOT\\Microsoft\\Windows\\Storage");
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return disks;
    }

    const auto objects = storage.query(QStringLiteral("SELECT * FROM MSFT_Disk WHERE BusType = 7"));
    if (objects.isEmpty() && !storage.lastError().isEmpty()) {
        if (error) {
            *error = storage.lastError();
        }
        return disks;
    }

    for (const WmiObject &object : objects) {
        disks.push_back(diskInfoFromObject(object));
    }

    return disks;
}

bool DiskEnumerator::diskByNumber(quint32 diskNumber, DiskInfo *disk, QString *error) const
{
    WmiConnection storage(L"ROOT\\Microsoft\\Windows\\Storage");
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    const auto objects = storage.query(QStringLiteral("SELECT * FROM MSFT_Disk WHERE Number = %1 AND BusType = 7").arg(diskNumber));
    if (objects.isEmpty()) {
        if (error) {
            const QString detail = storage.lastError();
            *error = detail.isEmpty()
                ? QStringLiteral("Disk %1 is no longer reported as a USB disk. Refresh and select the USB again.").arg(diskNumber)
                : detail;
        }
        return false;
    }

    if (disk) {
        *disk = diskInfoFromObject(objects.first());
    }
    return true;
}

DiskInfo DiskEnumerator::diskInfoFromObject(const WmiObject &object) const
{
    DiskInfo disk;
    disk.number = object.uintValue(L"Number");
    disk.busType = object.uintValue(L"BusType");
    disk.name = object.stringValue(L"FriendlyName");
    disk.uniqueId = object.stringValue(L"UniqueId");
    disk.serialNumber = object.stringValue(L"SerialNumber");
    disk.path = object.stringValue(L"Path");
    disk.size = object.uint64Value(L"Size");
    disk.sectorSize = object.uintValue(L"LogicalSectorSize", 512);
    disk.health = object.stringValue(L"HealthStatus");
    disk.partitionStyle = object.stringValue(L"PartitionStyle");
    disk.isBoot = object.boolValue(L"IsBoot");
    disk.isSystem = object.boolValue(L"IsSystem");
    disk.isReadOnly = object.boolValue(L"IsReadOnly");
    disk.isOffline = object.boolValue(L"IsOffline");
    disk.driveLetters = driveLettersForDisk(disk.number);
    disk.labels = labelsForLetters(disk.driveLetters);
    return disk;
}

QStringList DiskEnumerator::driveLettersForDisk(quint32 diskNumber) const
{
    QStringList result;
    const DWORD drives = GetLogicalDrives();
    for (wchar_t letter = L'A'; letter <= L'Z'; ++letter) {
        const int bit = letter - L'A';
        if ((drives & (1u << bit)) == 0) {
            continue;
        }

        const QString root = QStringLiteral("%1:\\").arg(QChar(letter));
        const QString path = QStringLiteral("\\\\.\\%1:").arg(QChar(letter));
        HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(path.utf16()), GENERIC_READ,
                                    FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }

        const bool belongs = volumeHasDiskExtent(handle, diskNumber);
        CloseHandle(handle);
        if (belongs) {
            result << root.left(2);
        }
    }
    return result;
}

QStringList DiskEnumerator::labelsForLetters(const QStringList &letters) const
{
    QStringList labels;
    for (const QString &letter : letters) {
        const QString root = letter.left(1) + QStringLiteral(":\\");
        wchar_t volumeName[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(reinterpret_cast<LPCWSTR>(root.utf16()), volumeName, MAX_PATH,
                                  nullptr, nullptr, nullptr, nullptr, 0)) {
            const QString label = QString::fromWCharArray(volumeName);
            if (!label.isEmpty()) {
                labels << label;
            }
        }
    }
    return labels;
}

}

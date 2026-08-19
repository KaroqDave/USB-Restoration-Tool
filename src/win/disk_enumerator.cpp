#include "win/disk_enumerator.h"

#include "win/windows_util.h"
#include "win/wmi.h"

#include <algorithm>

namespace usbrestore {

namespace {

constexpr auto StorageNamespace = L"ROOT\\Microsoft\\Windows\\Storage";

} // namespace

QVector<DiskInfo> DiskEnumerator::listUsbDisks(QString *error) const
{
    QVector<DiskInfo> disks;
    WmiConnection storage(StorageNamespace);
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return disks;
    }

    const auto objects =
        storage.query(QStringLiteral("SELECT * FROM MSFT_Disk WHERE BusType = %1").arg(UsbBusType));
    if (objects.isEmpty() && !storage.lastError().isEmpty()) {
        if (error) {
            *error = storage.lastError();
        }
        return disks;
    }

    disks.reserve(objects.size());
    for (const WmiObject &object : objects) {
        disks.push_back(diskInfoFromObject(object));
    }

    std::sort(disks.begin(), disks.end(), [](const DiskInfo &left, const DiskInfo &right) {
        return left.number < right.number;
    });
    return disks;
}

bool DiskEnumerator::diskByNumber(quint32 diskNumber, DiskInfo *disk, QString *error) const
{
    WmiConnection storage(StorageNamespace);
    if (!storage.isValid()) {
        if (error) {
            *error = storage.lastError();
        }
        return false;
    }

    // BusType is part of the query rather than a check afterwards, so a disk
    // number that has been reused by a non-USB device returns nothing at all.
    const auto objects = storage.query(QStringLiteral("SELECT * FROM MSFT_Disk WHERE Number = %1 AND BusType = %2")
                                           .arg(diskNumber)
                                           .arg(UsbBusType));
    if (objects.isEmpty()) {
        if (error) {
            const QString detail = storage.lastError();
            *error = detail.isEmpty() ? QStringLiteral("Disk %1 is no longer reported as a USB disk. Refresh and "
                                                       "select the USB again.")
                                            .arg(diskNumber)
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
    // The path the raw layer reopens, and the number Disk Management shows.
    disk.deviceId = QStringLiteral("\\\\.\\PhysicalDrive%1").arg(disk.number);
    disk.displayId = QStringLiteral("Disk %1").arg(disk.number);
    disk.busType = object.uintValue(L"BusType");
    disk.name = object.stringValue(L"FriendlyName").trimmed();
    disk.uniqueId = object.stringValue(L"UniqueId").trimmed();
    disk.serialNumber = object.stringValue(L"SerialNumber").trimmed();
    disk.path = object.stringValue(L"Path").trimmed();
    disk.size = object.uint64Value(L"Size");
    disk.sectorSize = object.uintValue(L"LogicalSectorSize", 512);
    disk.health = static_cast<quint16>(object.uintValue(L"HealthStatus"));
    disk.partitionStyle = static_cast<quint16>(object.uintValue(L"PartitionStyle"));
    disk.isBoot = object.boolValue(L"IsBoot");
    disk.isSystem = object.boolValue(L"IsSystem");
    disk.isReadOnly = object.boolValue(L"IsReadOnly");
    disk.isOffline = object.boolValue(L"IsOffline");
    disk.mountPoints = driveLettersForDisk(disk.number);
    volumeInfoForLetters(disk.mountPoints, &disk.labels, &disk.fileSystems);
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

        // Every letter is checked, C: included. A USB disk that somehow holds
        // the system drive has to be visible to the safety check, not filtered
        // out before it gets there.
        const HANDLE handle = openVolumeForQuery(QChar(letter));
        if (handle == INVALID_HANDLE_VALUE) {
            continue;
        }
        const bool belongs = volumeHasDiskExtent(handle, diskNumber);
        CloseHandle(handle);
        if (belongs) {
            result << QStringLiteral("%1:").arg(QChar(letter));
        }
    }
    return result;
}

void DiskEnumerator::volumeInfoForLetters(const QStringList &letters,
                                          QStringList *labels,
                                          QStringList *fileSystems) const
{
    for (const QString &letter : letters) {
        const QString root = letter.left(1) + QStringLiteral(":\\");
        wchar_t volumeName[MAX_PATH + 1] = {};
        wchar_t fileSystemName[MAX_PATH + 1] = {};
        if (GetVolumeInformationW(reinterpret_cast<LPCWSTR>(root.utf16()),
                                  volumeName,
                                  MAX_PATH,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  fileSystemName,
                                  MAX_PATH)) {
            const QString label = QString::fromWCharArray(volumeName).trimmed();
            if (labels && !label.isEmpty()) {
                *labels << label;
            }
            const QString fileSystem = QString::fromWCharArray(fileSystemName).trimmed();
            if (fileSystems && !fileSystem.isEmpty() && !fileSystems->contains(fileSystem)) {
                *fileSystems << fileSystem;
            }
        }
    }
}

} // namespace usbrestore

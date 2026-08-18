#include "core/disk.h"
#include "core/partition_table.h"
#include "core/restore_protocol.h"
#include "core/safety.h"

#include <QtTest/QtTest>

using namespace usbrestore;

namespace {

// A disk that passes every safety check, so each test can break exactly one
// thing and see that the one thing is what gets refused.
DiskInfo healthyUsbDisk()
{
    DiskInfo disk;
    disk.deviceId = QStringLiteral("/dev/sdb");
    disk.displayId = QStringLiteral("/dev/sdb");
    disk.busType = UsbBusType;
    disk.name = QStringLiteral("SanDisk Ultra USB 3.0");
    disk.serialNumber = QStringLiteral("USB123");
    disk.uniqueId = QStringLiteral("usb-SanDisk_Ultra_USB123-0:0");
    disk.path = QStringLiteral("/sys/devices/pci0000:00/usb2/2-1/block/sdb");
    disk.size = 16ull * 1024ull * 1024ull * 1024ull;
    disk.sectorSize = 512;
    disk.mountPoints = {QStringLiteral("/run/media/dave/USB")};
    return disk;
}

DiskInfo healthyWindowsDisk()
{
    DiskInfo disk = healthyUsbDisk();
    disk.deviceId = QStringLiteral("\\\\.\\PhysicalDrive2");
    disk.displayId = QStringLiteral("Disk 2");
    disk.number = 2;
    disk.path = QStringLiteral("\\\\?\\usbstor#disk&ven_sandisk");
    disk.mountPoints = {QStringLiteral("E:")};
    return disk;
}

RestoreGuard linuxGuard()
{
    RestoreGuard guard;
    guard.mountPoints = {QStringLiteral("/"), QStringLiteral("/boot"), QStringLiteral("/home")};
    guard.deviceIds = {QStringLiteral("/dev/nvme0n1")};
    return guard;
}

RestoreGuard windowsGuard()
{
    RestoreGuard guard;
    guard.mountPoints = {QStringLiteral("C")};
    return guard;
}

quint32 readLe32(const QByteArray &data, int offset)
{
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<quint32>(static_cast<quint8>(data.at(offset + i))) << (8 * i);
    }
    return value;
}

quint64 readLe64(const QByteArray &data, int offset)
{
    quint64 value = 0;
    for (int i = 0; i < 8; ++i) {
        value |= static_cast<quint64>(static_cast<quint8>(data.at(offset + i))) << (8 * i);
    }
    return value;
}

PartitionTableRequest tableRequestFor(std::uint64_t diskSize, quint32 sectorSize, PartitionStyle style)
{
    PartitionTableRequest request;
    request.style = style;
    request.diskSize = diskSize;
    request.sectorSize = sectorSize;
    request.layout = calculateGptLayout(diskSize, sectorSize);
    request.diskGuid = QByteArray(16, '\x11');
    request.partitionGuid = QByteArray(16, '\x22');
    request.partitionName = QStringLiteral("USB");
    return request;
}

} // namespace

class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void formatsByteSizes()
    {
        QCOMPARE(formatByteSize(0), QStringLiteral("0 B"));
        QCOMPARE(formatByteSize(512), QStringLiteral("512 B"));
        QCOMPARE(formatByteSize(16ull * 1024ull * 1024ull * 1024ull), QStringLiteral("16.00 GB"));
    }

    void namesStorageEnums()
    {
        QCOMPARE(partitionStyleName(static_cast<quint16>(PartitionStyle::Gpt)), QStringLiteral("GPT"));
        QCOMPARE(partitionStyleName(static_cast<quint16>(PartitionStyle::Mbr)), QStringLiteral("MBR"));
        QCOMPARE(partitionStyleName(static_cast<quint16>(PartitionStyle::Unknown)), QStringLiteral("RAW / unknown"));
        QCOMPARE(partitionStyleLabel(PartitionStyle::Gpt), QStringLiteral("GPT"));
        QCOMPARE(fileSystemTypeName(FileSystemType::ExFat), QStringLiteral("exFAT"));
        QCOMPARE(fileSystemTypeName(FileSystemType::Fat32), QStringLiteral("FAT32"));
        QCOMPARE(fileSystemTypeName(FileSystemType::Ntfs), QStringLiteral("NTFS"));
        QCOMPARE(fileSystemTypeName(FileSystemType::Ext4), QStringLiteral("ext4"));
        QCOMPARE(fileSystemTypeToken(FileSystemType::ExFat), QStringLiteral("exfat"));
        FileSystemType parsed = FileSystemType::Ntfs;
        QVERIFY(parseFileSystemType(QStringLiteral("ext4"), &parsed));
        QCOMPARE(parsed, FileSystemType::Ext4);
        QVERIFY(!parseFileSystemType(QStringLiteral("btrfs")));
        QCOMPARE(healthStatusName(static_cast<quint16>(HealthStatus::Healthy)), QStringLiteral("Healthy"));
        QCOMPARE(busTypeName(UsbBusType), QStringLiteral("USB"));
        QCOMPARE(busTypeName(17), QStringLiteral("NVMe"));
    }

    void acceptsOrdinaryUsbDisk()
    {
        QVERIFY(isSafeRestoreTarget(healthyUsbDisk(), linuxGuard()));
        QVERIFY(isSafeRestoreTarget(healthyWindowsDisk(), windowsGuard()));
    }

    void refusesNonUsbBus()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.busType = 11; // SATA
        QString reason;
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("SATA")));
    }

    // The 1.0.0 safety check let a bus type of 0 through as "unknown is fine".
    // A disk whose bus cannot be read is exactly the disk not to erase.
    void refusesUnknownBus()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.busType = 0;
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard()));
    }

    void refusesDiskWithNoDeviceId()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.deviceId.clear();
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard()));
    }

    void refusesGuardedDevice()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.deviceId = QStringLiteral("/dev/nvme0n1");
        QString reason;
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("depends on")));
    }

    // An empty entry in the guard must match nothing. Getting this backwards
    // would refuse every disk on a machine that reported one blank device.
    void emptyGuardEntriesMatchNothing()
    {
        RestoreGuard guard;
        guard.deviceIds = {QString(), QStringLiteral("  ")};
        guard.mountPoints = {QString()};
        QVERIFY(isSafeRestoreTarget(healthyUsbDisk(), guard));
    }

    void refusesProtectedWindowsDriveLetter()
    {
        DiskInfo disk = healthyWindowsDisk();
        disk.mountPoints = {QStringLiteral("C:")};
        QString reason;
        QVERIFY(!isSafeRestoreTarget(disk, windowsGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("protected")));
    }

    void matchesDriveLettersHoweverSpelled()
    {
        QVERIFY(isProtectedMountPoint(QStringLiteral("e:\\"), {QStringLiteral("E:")}));
        QVERIFY(isProtectedMountPoint(QStringLiteral("E:"), {QStringLiteral("e")}));
        QVERIFY(!isProtectedMountPoint(QStringLiteral("F:"), {QStringLiteral("E:")}));
    }

    void refusesProtectedPosixMountPoints()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.mountPoints = {QStringLiteral("/home")};
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard()));

        // A trailing slash is the same directory.
        disk.mountPoints = {QStringLiteral("/home/")};
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard()));
    }

    // A disk mounted at "/" holds "/boot" whether or not "/boot" is a separate
    // mount, so containment has to count, not just equality.
    void refusesMountPointContainingAProtectedPath()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.mountPoints = {QStringLiteral("/")};
        QVERIFY(!isSafeRestoreTarget(disk, linuxGuard()));

        QVERIFY(isProtectedMountPoint(QStringLiteral("/"), {QStringLiteral("/boot")}));
        QVERIFY(!isProtectedMountPoint(QStringLiteral("/run/media/dave"), {QStringLiteral("/boot")}));
        // "/bootstrap" is not "/boot", however similar the strings look.
        QVERIFY(!isProtectedMountPoint(QStringLiteral("/bootstrap"), {QStringLiteral("/boot")}));
    }

    void refusesBootSystemOfflineAndReadOnlyDisks()
    {
        QString reason;

        DiskInfo boot = healthyUsbDisk();
        boot.isBoot = true;
        QVERIFY(!isSafeRestoreTarget(boot, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("boot")));

        DiskInfo offline = healthyUsbDisk();
        offline.isOffline = true;
        reason.clear();
        QVERIFY(!isSafeRestoreTarget(offline, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("offline")));

        DiskInfo readOnly = healthyUsbDisk();
        readOnly.isReadOnly = true;
        reason.clear();
        QVERIFY(!isSafeRestoreTarget(readOnly, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("read-only")));
    }

    void refusesEmptyTinyAndOddlySectoredDisks()
    {
        DiskInfo empty = healthyUsbDisk();
        empty.size = 0;
        QVERIFY(!isSafeRestoreTarget(empty, linuxGuard()));

        DiskInfo tiny = healthyUsbDisk();
        tiny.size = 4ull * 1024ull * 1024ull;
        QVERIFY(!isSafeRestoreTarget(tiny, linuxGuard()));

        DiskInfo oddSectors = healthyUsbDisk();
        oddSectors.sectorSize = 511;
        QString reason;
        QVERIFY(!isSafeRestoreTarget(oddSectors, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("sector size")));
    }

    void acceptsUnchangedRestoreTarget()
    {
        const DiskInfo selected = healthyUsbDisk();
        QVERIFY(isSameRestoreTarget(selected, selected));
    }

    void rejectsMovedDevice()
    {
        const DiskInfo selected = healthyUsbDisk();
        DiskInfo current = selected;
        current.deviceId = QStringLiteral("/dev/sdc");

        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, current, &reason));
        QVERIFY(reason.contains(QStringLiteral("moved")));
    }

    void rejectsChangedSerialNumber()
    {
        const DiskInfo selected = healthyUsbDisk();
        DiskInfo current = selected;
        current.serialNumber = QStringLiteral("replacement");

        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, current, &reason));
        QVERIFY(reason.contains(QStringLiteral("serial")));
    }

    // udev's usb- form is vendor_product_serial, but the product itself may
    // contain underscores. Taking the last field invented a serial from the
    // product name, and isSameRestoreTarget() then treated two identical
    // serial-less sticks as the same disk.
    void parsesSerialFromUsbByIdLink()
    {
        struct Case {
            const char *link;
            const char *product;
            const char *serial;
        };
        const Case cases[] = {
            {"usb-SanDisk_Ultra_4C530001-0:0", "SanDisk Ultra", "4C530001"},
            {"usb-SanDisk_Ultra_USB123-0:0", "SanDisk Ultra USB 3.0", "USB123"},
            {"usb-Generic_Flash_Disk-0:0", "Generic Flash Disk", ""},
            {"usb-Vendor_Model-0:0", "Vendor Model", ""},
            {"wwn-0x5000c500", "Vendor", ""},
            {"", "", ""},
        };
        for (const auto &c : cases) {
            QCOMPARE(
                serialFromUsbByIdLink(QString::fromUtf8(c.link), QString::fromUtf8(c.product)),
                QString::fromUtf8(c.serial));
        }
    }

    // The 0.1.0 check stopped at the first strong identifier that matched, so a
    // device keeping its serial while changing its path slipped past.
    void rejectsChangedPathEvenWhenSerialMatches()
    {
        const DiskInfo selected = healthyUsbDisk();
        DiskInfo current = selected;
        current.path = QStringLiteral("/sys/devices/pci0000:00/usb2/2-4/block/sdb");

        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, current, &reason));
        QVERIFY(reason.contains(QStringLiteral("device path")));
    }

    void ignoresIdentifiersOnlyOneSideReports()
    {
        DiskInfo selected = healthyUsbDisk();
        selected.uniqueId.clear();
        QVERIFY(isSameRestoreTarget(selected, healthyUsbDisk()));
    }

    void rejectsChangedSize()
    {
        const DiskInfo selected = healthyUsbDisk();
        DiskInfo resized = selected;
        resized.size += 512;

        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, resized, &reason));
        QVERIFY(reason.contains(QStringLiteral("size")));
    }

    void identifiesLargeRestoreTarget()
    {
        DiskInfo small = healthyUsbDisk();
        small.size = 64ull * 1024ull * 1024ull * 1024ull;
        QVERIFY(!isLargeRestoreTarget(small));
        QVERIFY(largeRestoreTargetWarning(small).isEmpty());

        DiskInfo large = healthyUsbDisk();
        large.size = 256ull * 1024ull * 1024ull * 1024ull;
        QVERIFY(isLargeRestoreTarget(large));
        QVERIFY(largeRestoreTargetWarning(large).contains(QStringLiteral("Large USB disk")));
    }

    // Both ends land on a 1 MiB boundary, which is what every other partitioning
    // tool produces and what `sgdisk -v` expects to see.
    void calculatesAlignedGptLayout()
    {
        constexpr std::uint64_t oneMiB = 1024ull * 1024ull;
        const GptLayout layout = calculateGptLayout(16ull * 1024ull * 1024ull * 1024ull, 512);
        QCOMPARE(layout.startOffset, oneMiB);
        QVERIFY(layout.length > 15ull * 1024ull * 1024ull * 1024ull);
        QCOMPARE(layout.length % oneMiB, 0ull);
        QVERIFY(layout.startOffset + layout.length + 33ull * 512ull <= 16ull * 1024ull * 1024ull * 1024ull);
    }

    void alignsGptLayoutToLargeSectors()
    {
        constexpr std::uint64_t oneMiB = 1024ull * 1024ull;
        const GptLayout layout = calculateGptLayout(32ull * 1024ull * 1024ull * 1024ull, 4096);
        QCOMPARE(layout.startOffset % oneMiB, 0ull);
        QCOMPARE(layout.length % oneMiB, 0ull);
        QCOMPARE(layout.length % 4096ull, 0ull);
        QVERIFY(layout.startOffset + layout.length + 33ull * 4096ull <= 32ull * 1024ull * 1024ull * 1024ull);
    }

    // A disk whose size is not a whole number of megabytes still gets a legal
    // layout rather than one that runs past the backup header.
    void handlesDiskSizesThatAreNotRoundMegabytes()
    {
        const std::uint64_t odd = 16ull * 1024ull * 1024ull * 1024ull + 512ull * 777ull;
        const GptLayout layout = calculateGptLayout(odd, 512);
        QVERIFY(layout.length > 0);
        QCOMPARE(layout.length % (1024ull * 1024ull), 0ull);
        QVERIFY(layout.startOffset + layout.length + 33ull * 512ull <= odd);
    }

    void refusesGptLayoutOnDisksTooSmall()
    {
        QCOMPARE(calculateGptLayout(0, 512).length, 0ull);
        QCOMPARE(calculateGptLayout(1024ull * 1024ull, 512).length, 0ull);
    }

    void validatesSectorSizes()
    {
        QVERIFY(isSupportedSectorSize(512));
        QVERIFY(isSupportedSectorSize(4096));
        QVERIFY(!isSupportedSectorSize(0));
        QVERIFY(!isSupportedSectorSize(600));
    }

    void choosesFat32GeometryAtThe512ByteSectorMinimum()
    {
        constexpr std::uint64_t maximumVolume = 32ull * 1024ull * 1024ull * 1024ull;
        // 32 reserved sectors, two 512-sector FATs and the minimum 65,527
        // data clusters required by the Windows formatter.
        constexpr std::uint64_t minimumVolume = (32ull + 2ull * 512ull + 65527ull) * 512ull;

        QCOMPARE(fat32AllocationUnitSize(minimumVolume - 512, 512, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(minimumVolume, 512, maximumVolume), 512u);
    }

    void choosesFat32GeometryAtThe4096ByteSectorMinimum()
    {
        constexpr std::uint64_t maximumVolume = 32ull * 1024ull * 1024ull * 1024ull;
        // Larger sectors make each minimum-size cluster larger. Each FAT only
        // needs 64 sectors, but the data region now needs just over 256 MiB.
        constexpr std::uint64_t minimumVolume = (32ull + 2ull * 64ull + 65527ull) * 4096ull;

        QCOMPARE(fat32AllocationUnitSize(minimumVolume - 4096, 4096, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(minimumVolume, 4096, maximumVolume), 4096u);
    }

    void growsTheFat32AllocationUnitWithTheVolume()
    {
        constexpr std::uint64_t maximumVolume = 32ull * 1024ull * 1024ull * 1024ull;
        QCOMPARE(fat32AllocationUnitSize(1ull * 1024ull * 1024ull * 1024ull, 512, maximumVolume), 512u);
        QCOMPARE(fat32AllocationUnitSize(4ull * 1024ull * 1024ull * 1024ull, 512, maximumVolume), 1024u);
        QCOMPARE(fat32AllocationUnitSize(maximumVolume, 512, maximumVolume), 16u * 1024u);
    }

    void refusesFat32GeometryOutsideTheFormatterLimits()
    {
        constexpr std::uint64_t maximumVolume = 32ull * 1024ull * 1024ull * 1024ull;
        QCOMPARE(fat32AllocationUnitSize(maximumVolume + 512, 512, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(1024ull * 1024ull * 1024ull, 600, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(1024ull * 1024ull * 1024ull + 1, 512, maximumVolume), 0u);
    }

    // The numbers below were read out of dosfstools 4.2 (mkfs.fat.c) and then
    // checked against the binary itself: at each limit mkfs.vfat writes a clean
    // filesystem, and one mebibyte outside it warns instead of refusing. That
    // silent warning is the whole reason these limits are enforced here.
    void pinsTheMkfsFat32Minimum()
    {
        // Enough sectors for the 32 reserved, two FATs and MIN_CLUST_32 =
        // 65,525 single-sector clusters, rounded up to a whole mebibyte.
        QCOMPARE(minimumMkfsFat32VolumeBytes(512), 33ull * 1024ull * 1024ull);
        QCOMPARE(minimumMkfsFat32VolumeBytes(4096), 257ull * 1024ull * 1024ull);
    }

    void pinsTheMkfsFat32Maximum()
    {
        // UINT32_MAX sectors: mkfs.fat stores the count in 32 bits and clamps
        // anything longer, leaving the end of the disk unreachable.
        QCOMPARE(maximumMkfsFat32VolumeBytes(512), 4294967295ull * 512ull);
        QCOMPARE(maximumMkfsFat32VolumeBytes(4096), 4294967295ull * 4096ull);

        // Not the Windows limit. Copying that 32 GiB constant here would refuse
        // volumes mkfs.vfat handles perfectly well.
        QVERIFY(maximumMkfsFat32VolumeBytes(512) > 32ull * 1024ull * 1024ull * 1024ull);
    }

    void refusesMkfsFat32LimitsForUnsupportedSectorSizes()
    {
        QCOMPARE(minimumMkfsFat32VolumeBytes(600), 0ull);
        QCOMPARE(maximumMkfsFat32VolumeBytes(600), 0ull);
        QCOMPARE(minimumMkfsFat32VolumeBytes(0), 0ull);
        QCOMPARE(maximumMkfsFat32VolumeBytes(0), 0ull);
    }

    void leavesEveryOrdinaryUsbSizeBetweenTheMkfsFat32Limits()
    {
        for (const quint32 sectorSize : {512u, 4096u}) {
            QVERIFY(minimumMkfsFat32VolumeBytes(sectorSize) < maximumMkfsFat32VolumeBytes(sectorSize));

            // A 16 GB stick, the case the check must not get in the way of.
            const std::uint64_t length = calculateGptLayout(16ull * 1024ull * 1024ull * 1024ull, sectorSize).length;
            QVERIFY(length > minimumMkfsFat32VolumeBytes(sectorSize));
            QVERIFY(length < maximumMkfsFat32VolumeBytes(sectorSize));
        }

        // A 4 TB USB hard disk with 512-byte sectors is over the limit, which
        // is what the Windows-shaped 32 GiB constant would have got wrong in
        // the other direction.
        const std::uint64_t large = calculateGptLayout(4000ull * 1000ull * 1000ull * 1000ull, 512).length;
        QVERIFY(large > maximumMkfsFat32VolumeBytes(512));

        // The same disk reporting 4096-byte sectors is not.
        const std::uint64_t largeWithBigSectors =
            calculateGptLayout(4000ull * 1000ull * 1000ull * 1000ull, 4096).length;
        QVERIFY(largeWithBigSectors < maximumMkfsFat32VolumeBytes(4096));
    }

    void describesDiskForPrompts()
    {
        const QString description = describeDisk(healthyWindowsDisk());
        QVERIFY(description.contains(QStringLiteral("Disk 2")));
        QVERIFY(description.contains(QStringLiteral("SanDisk")));
        QVERIFY(description.contains(QStringLiteral("16.00 GB")));
    }

    // --- Partition table bytes -------------------------------------------
    // The Linux backend writes these itself, so a wrong byte here is a disk
    // that no longer mounts. Windows builds the same table through an IOCTL
    // and never runs this code, which is exactly why it is worth pinning down.

    void computesTheCrc32GptSpecifies()
    {
        // The check value every CRC-32 implementation is measured against.
        QCOMPARE(gptCrc32(QByteArrayLiteral("123456789")), 0xCBF43926u);
        QCOMPARE(gptCrc32(QByteArray()), 0u);
    }

    void writesTheBasicDataPartitionGuidInMixedEndianForm()
    {
        const QByteArray guid = basicDataPartitionTypeGuid();
        QCOMPARE(guid.size(), 16);
        QCOMPARE(guid, QByteArrayLiteral("\xA2\xA0\xD0\xEB\xE5\xB9\x33\x44\x87\xC0\x68\xB6\xB7\x26\x99\xC7"));
    }

    void writesTheLinuxFilesystemGuidInMixedEndianForm()
    {
        const QByteArray guid = linuxFilesystemPartitionTypeGuid();
        QCOMPARE(guid.size(), 16);
        QCOMPARE(guid, QByteArrayLiteral("\xAF\x3D\xC6\x0F\x83\x84\x72\x47\x8E\x79\x3D\x69\xD8\x47\x7D\xE4"));
        QCOMPARE(gptPartitionTypeGuid(FileSystemType::Ext4), guid);
        QCOMPARE(gptPartitionTypeGuid(FileSystemType::ExFat), basicDataPartitionTypeGuid());
        QCOMPARE(gptPartitionTypeGuid(FileSystemType::Fat32), basicDataPartitionTypeGuid());
        QCOMPARE(gptPartitionTypeGuid(FileSystemType::Ntfs), basicDataPartitionTypeGuid());
    }

    void buildsAGptHeaderThatChecksOutAgainstItself()
    {
        const auto request = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        QVERIFY(isWritablePartitionRequest(request));

        const QByteArray primary = buildGptPrimary(request);
        // Protective MBR, header sector, and 32 sectors of entry array.
        QCOMPARE(primary.size(), 34 * 512);

        // Protective MBR: one 0xEE partition starting at LBA 1.
        QCOMPARE(static_cast<quint8>(primary.at(450)), quint8(0xEE));
        QCOMPARE(readLe32(primary, 454), 1u);
        QCOMPARE(static_cast<quint8>(primary.at(510)), quint8(0x55));
        QCOMPARE(static_cast<quint8>(primary.at(511)), quint8(0xAA));

        const QByteArray header = primary.mid(512, 512);
        QCOMPARE(header.left(8), QByteArrayLiteral("EFI PART"));
        QCOMPARE(readLe32(header, 12), 92u);
        QCOMPARE(readLe64(header, 24), 1ull); // MyLBA
        QCOMPARE(readLe64(header, 72), 2ull); // Entry array LBA
        QCOMPARE(readLe32(header, 80), 128u); // Entry count
        QCOMPARE(readLe32(header, 84), 128u); // Entry size

        // The header checksum covers the first 92 bytes with the checksum field
        // itself read as zero. Recomputing it that way must reproduce it.
        QByteArray zeroed = header.left(92);
        zeroed.replace(16, 4, QByteArray(4, '\0'));
        QCOMPARE(readLe32(header, 16), gptCrc32(zeroed));

        // And the entry-array checksum must match the array that follows.
        QCOMPARE(readLe32(header, 88), gptCrc32(primary.mid(1024, 128 * 128)));
    }

    void placesTheGptPartitionWhereTheLayoutSaid()
    {
        const auto request = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        const QByteArray entry = buildGptPrimary(request).mid(1024, 128);

        QCOMPARE(entry.left(16), basicDataPartitionTypeGuid());
        QCOMPARE(entry.mid(16, 16), request.partitionGuid);
        QCOMPARE(readLe64(entry, 32), request.layout.startOffset / 512);
        // The end LBA is inclusive: the last sector in the partition.
        QCOMPARE(readLe64(entry, 40), (request.layout.startOffset + request.layout.length) / 512 - 1);
        // "USB" as UTF-16LE.
        QCOMPARE(entry.mid(56, 6), QByteArrayLiteral("U\0S\0B\0"));
    }

    void writesTheLinuxFilesystemGuidForExt4()
    {
        auto request = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        request.fileSystem = FileSystemType::Ext4;
        const QByteArray entry = buildGptPrimary(request).mid(1024, 128);
        QCOMPARE(entry.left(16), linuxFilesystemPartitionTypeGuid());
    }

    void writesFat32AndLinuxMbrTypeBytes()
    {
        auto fat32 = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Mbr);
        fat32.fileSystem = FileSystemType::Fat32;
        QCOMPARE(static_cast<quint8>(buildMbr(fat32).at(450)), MbrFat32LbaPartitionType);

        auto ext4 = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Mbr);
        ext4.fileSystem = FileSystemType::Ext4;
        QCOMPARE(static_cast<quint8>(buildMbr(ext4).at(450)), MbrLinuxPartitionType);
    }

    void keepsTheGptPartitionInsideTheUsableRange()
    {
        for (const quint32 sectorSize : {512u, 4096u}) {
            const auto request = tableRequestFor(32ull * 1024ull * 1024ull * 1024ull, sectorSize, PartitionStyle::Gpt);
            const QByteArray header = buildGptPrimary(request).mid(sectorSize, sectorSize);

            const quint64 firstUsable = readLe64(header, 40);
            const quint64 lastUsable = readLe64(header, 48);
            const QByteArray entry = buildGptPrimary(request).mid(2 * sectorSize, 128);
            const quint64 first = readLe64(entry, 32);
            const quint64 last = readLe64(entry, 40);

            QVERIFY(first >= firstUsable);
            QVERIFY(last <= lastUsable);
        }
    }

    void mirrorsThePrimaryHeaderInTheBackup()
    {
        const auto request = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        const QByteArray backup = buildGptBackup(request);
        QCOMPARE(backup.size(), 33 * 512);

        const QByteArray header = backup.right(512);
        QCOMPARE(header.left(8), QByteArrayLiteral("EFI PART"));

        const quint64 lastLba = request.diskSize / 512 - 1;
        QCOMPARE(readLe64(header, 24), lastLba);      // MyLBA is the last sector
        QCOMPARE(readLe64(header, 32), 1ull);         // Alternate is the primary
        QCOMPARE(readLe64(header, 72), lastLba - 32); // Its own entry array

        // The backup must land exactly on that entry-array LBA.
        QCOMPARE(gptBackupOffset(request), (lastLba - 32) * 512);
        QCOMPARE(gptBackupOffset(request) + static_cast<std::uint64_t>(backup.size()), request.diskSize);

        // Both copies describe the same partition.
        QCOMPARE(backup.left(128), buildGptPrimary(request).mid(1024, 128));
    }

    void buildsAnMbrForOlderDevices()
    {
        const auto request = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Mbr);
        QVERIFY(isWritablePartitionRequest(request));

        const QByteArray mbr = buildMbr(request);
        QCOMPARE(mbr.size(), 512);
        // A non-zero disk signature: Windows tells disks apart by it and gets
        // confused when two share one.
        QCOMPARE(mbr.mid(440, 4), request.diskGuid.left(4));
        QVERIFY(mbr.mid(440, 4) != QByteArray(4, '\0'));
        QCOMPARE(static_cast<quint8>(mbr.at(446)), quint8(0x00)); // Not bootable
        QCOMPARE(static_cast<quint8>(mbr.at(450)), MbrExFatPartitionType);
        QCOMPARE(mbrPartitionType(FileSystemType::ExFat), MbrExFatPartitionType);
        QCOMPARE(mbrPartitionType(FileSystemType::Ntfs), MbrExFatPartitionType);
        QCOMPARE(mbrPartitionType(FileSystemType::Fat32), MbrFat32LbaPartitionType);
        QCOMPARE(mbrPartitionType(FileSystemType::Ext4), MbrLinuxPartitionType);
        QCOMPARE(readLe32(mbr, 454), static_cast<quint32>(request.layout.startOffset / 512));
        QCOMPARE(readLe32(mbr, 458), static_cast<quint32>(request.layout.length / 512));
        QCOMPARE(static_cast<quint8>(mbr.at(510)), quint8(0x55));
        QCOMPARE(static_cast<quint8>(mbr.at(511)), quint8(0xAA));
    }

    void padsTheMbrToAWholeSectorOn4KnDisks()
    {
        const auto request = tableRequestFor(32ull * 1024ull * 1024ull * 1024ull, 4096, PartitionStyle::Mbr);
        const QByteArray mbr = buildMbr(request);
        // A raw write to a 4 Kn device has to be a whole sector.
        QCOMPARE(mbr.size(), 4096);
        QCOMPARE(static_cast<quint8>(mbr.at(510)), quint8(0x55));
    }

    void refusesUnwritablePartitionRequests()
    {
        QString reason;

        auto badSector = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        badSector.sectorSize = 999;
        QVERIFY(!isWritablePartitionRequest(badSector, &reason));

        auto tiny = tableRequestFor(1024ull * 1024ull, 512, PartitionStyle::Gpt);
        QVERIFY(!isWritablePartitionRequest(tiny, &reason));

        auto noGuid = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        noGuid.diskGuid.clear();
        QVERIFY(!isWritablePartitionRequest(noGuid, &reason));
        QVERIFY(reason.contains(QStringLiteral("identifier")));

        auto mbrNoSignature = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Mbr);
        mbrNoSignature.diskGuid.clear();
        QVERIFY(!isWritablePartitionRequest(mbrNoSignature, &reason));
        QVERIFY(reason.contains(QStringLiteral("identifier")));

        auto misaligned = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        misaligned.layout.startOffset += 1;
        QVERIFY(!isWritablePartitionRequest(misaligned, &reason));
        QVERIFY(reason.contains(QStringLiteral("aligned")));

        auto pastTheEnd = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        pastTheEnd.layout.length += 64ull * 1024ull * 1024ull;
        QVERIFY(!isWritablePartitionRequest(pastTheEnd, &reason));
        QVERIFY(reason.contains(QStringLiteral("past the end")));

        auto insideHeaders = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        insideHeaders.layout.startOffset = 2 * 512;
        QVERIFY(!isWritablePartitionRequest(insideHeaders, &reason));
        QVERIFY(reason.contains(QStringLiteral("GPT")));

        // Covers the last sector of the disk, so it is still "inside" the
        // disk, but it overwrites the backup GPT. The past-the-end check
        // used to let this through.
        auto overlapsBackup = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        overlapsBackup.layout.length = overlapsBackup.diskSize - overlapsBackup.layout.startOffset;
        QVERIFY(!isWritablePartitionRequest(overlapsBackup, &reason));
        QVERIFY(reason.contains(QStringLiteral("GPT")));
    }

    // MBR addresses sectors in 32 bits, which runs out at 2 TiB. A 4 TB
    // external drive with MBR selected must be refused, not quietly truncated.
    void refusesMbrBeyondItsAddressingLimit()
    {
        auto request = tableRequestFor(4ull * 1024ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Mbr);
        QString reason;
        QVERIFY(!isWritablePartitionRequest(request, &reason));
        QVERIFY(reason.contains(QStringLiteral("MBR")));

        // The same disk is fine under GPT.
        request.style = PartitionStyle::Gpt;
        QVERIFY(isWritablePartitionRequest(request));
    }

    // Everything below covers the boundary between the unprivileged GUI and the
    // privileged helper. These arguments arrive from a process that has no
    // privilege at all, so each test is a way the helper must refuse to be
    // talked into something rather than a way it must cooperate.

    void roundTripsRestoreArguments()
    {
        const DiskInfo disk = healthyUsbDisk();
        const QStringList arguments =
            buildRestoreArguments(disk, PartitionStyle::Mbr, FileSystemType::Ntfs, QStringLiteral("USB"));

        RestoreArguments parsed;
        QString error;
        QVERIFY2(parseRestoreArguments(arguments, &parsed, &error), qPrintable(error));
        QCOMPARE(parsed.style, PartitionStyle::Mbr);
        QCOMPARE(parsed.fileSystem, FileSystemType::Ntfs);
        QCOMPARE(parsed.volumeLabel, QStringLiteral("USB"));
        QCOMPARE(parsed.expected.deviceId, disk.deviceId);
        QCOMPARE(parsed.expected.size, disk.size);
        QCOMPARE(parsed.expected.sectorSize, disk.sectorSize);
        QCOMPARE(parsed.expected.serialNumber, disk.serialNumber);
        QCOMPARE(parsed.expected.uniqueId, disk.uniqueId);
        QCOMPARE(parsed.expected.path, disk.path);
        QCOMPARE(parsed.expected.name, disk.name);

        // What the round trip is for: the disk the helper enumerates for itself
        // still reads as the disk the GUI was talking about.
        QVERIFY(isSameRestoreTarget(parsed.expected, disk));
    }

    void roundTripsExt4FilesystemArgument()
    {
        RestoreArguments parsed;
        QVERIFY(parseRestoreArguments(
            buildRestoreArguments(healthyUsbDisk(), PartitionStyle::Gpt, FileSystemType::Ext4, QStringLiteral("USB")),
            &parsed));
        QCOMPARE(parsed.fileSystem, FileSystemType::Ext4);
        QCOMPARE(parsed.style, PartitionStyle::Gpt);
    }

    // The helper must never be able to conclude "this is a USB disk" from what
    // it was told. A DiskInfo assembled purely from arguments has no bus, and a
    // disk with no bus is refused — so a mistake that fed one to the safety
    // check would fail closed rather than open.
    void argumentsNeverAssertTheBus()
    {
        RestoreArguments parsed;
        QVERIFY(parseRestoreArguments(
            buildRestoreArguments(healthyUsbDisk(), PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB")),
            &parsed));

        QCOMPARE(parsed.expected.busType, 0u);
        QString reason;
        QVERIFY(!isSafeRestoreTarget(parsed.expected, linuxGuard(), &reason));
        QVERIFY(reason.contains(QStringLiteral("bus")));
    }

    // An identifier the GUI does not have is left out entirely, because
    // isSameRestoreTarget() reads a value only one side reports as no
    // information. Sending an empty string instead would be the same thing said
    // less clearly.
    void omitsIdentifiersTheGuiDoesNotHave()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.serialNumber.clear();
        disk.uniqueId.clear();

        const QStringList arguments =
            buildRestoreArguments(disk, PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB"));
        QVERIFY(!arguments.contains(QStringLiteral("--expect-serial")));
        QVERIFY(!arguments.contains(QStringLiteral("--expect-unique-id")));

        RestoreArguments parsed;
        QVERIFY(parseRestoreArguments(arguments, &parsed));
        QVERIFY(parsed.expected.serialNumber.isEmpty());
    }

    void refusesDevicePathsThatAreNotBlockDevices()
    {
        QVERIFY(isPlausibleDeviceNodePath(QStringLiteral("/dev/sdb")));
        QVERIFY(isPlausibleDeviceNodePath(QStringLiteral("/dev/nvme0n1")));
        QVERIFY(isPlausibleDeviceNodePath(QStringLiteral("/dev/mmcblk0")));

        // A separator of any kind is what would let a caller name something
        // outside /dev, so the name may not contain one at all.
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/../etc/shadow")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/disk/by-id/usb-SanDisk")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/etc/shadow")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/sd b")));
        QVERIFY(!isPlausibleDeviceNodePath(QString()));
    }

    // mkfs parses its own argv. A label beginning with "-" would reach it as
    // what looks like an option, which is the one way a string that is only
    // ever "USB" could turn into something else.
    void refusesLabelsThatCouldBeReadAsOptions()
    {
        QVERIFY(isValidVolumeLabel(QStringLiteral("USB")));
        QVERIFY(isValidVolumeLabel(QStringLiteral("USB_1-2")));
        QVERIFY(isValidVolumeLabel(QStringLiteral("11CHARSHERE")));

        QVERIFY(!isValidVolumeLabel(QStringLiteral("-f")));
        QVERIFY(!isValidVolumeLabel(QStringLiteral("-f /dev/sda")));
        QVERIFY(!isValidVolumeLabel(QStringLiteral("12CHARSHERE!")));
        QVERIFY(!isValidVolumeLabel(QStringLiteral("TWELVECHARSX")));
        QVERIFY(!isValidVolumeLabel(QStringLiteral(" USB")));
        QVERIFY(!isValidVolumeLabel(QStringLiteral("USB ")));
        QVERIFY(!isValidVolumeLabel(QStringLiteral("USB;rm -rf")));
        QVERIFY(!isValidVolumeLabel(QString()));
    }

    void refusesMalformedArguments()
    {
        const QStringList valid =
            buildRestoreArguments(healthyUsbDisk(), PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB"));
        QVERIFY(parseRestoreArguments(valid));

        const auto refuses = [](const QStringList &arguments, const QString &expected) {
            QString reason;
            const bool accepted = parseRestoreArguments(arguments, nullptr, &reason);
            return !accepted && reason.contains(expected);
        };

        QVERIFY(refuses(
            valid + QStringList{QStringLiteral("--exec"), QStringLiteral("/bin/sh")},
            QStringLiteral("not a recognised option")));
        QVERIFY(refuses(valid + QStringList{QStringLiteral("--expect-size")}, QStringLiteral("without a value")));
        QVERIFY(refuses(
            valid + QStringList{QStringLiteral("--device"), QStringLiteral("/dev/sdc")},
            QStringLiteral("more than once")));
        QVERIFY(refuses({}, QStringLiteral("--device")));

        QStringList badStyle = valid;
        badStyle[badStyle.indexOf(QStringLiteral("--style")) + 1] = QStringLiteral("ext4");
        QVERIFY(refuses(badStyle, QStringLiteral("--style")));

        QStringList badFileSystem = valid;
        badFileSystem[badFileSystem.indexOf(QStringLiteral("--filesystem")) + 1] = QStringLiteral("btrfs");
        QVERIFY(refuses(badFileSystem, QStringLiteral("--filesystem")));

        QStringList missingFileSystem = valid;
        const int fileSystemFlag = missingFileSystem.indexOf(QStringLiteral("--filesystem"));
        missingFileSystem.removeAt(fileSystemFlag + 1);
        missingFileSystem.removeAt(fileSystemFlag);
        QVERIFY(refuses(missingFileSystem, QStringLiteral("--filesystem")));

        QStringList zeroSize = valid;
        zeroSize[zeroSize.indexOf(QStringLiteral("--expect-size")) + 1] = QStringLiteral("0");
        QVERIFY(refuses(zeroSize, QStringLiteral("--expect-size")));

        QStringList wordSize = valid;
        wordSize[wordSize.indexOf(QStringLiteral("--expect-size")) + 1] = QStringLiteral("lots");
        QVERIFY(refuses(wordSize, QStringLiteral("--expect-size")));
    }

    // The output side of the same boundary. mkfs writes several lines at once
    // and the tool puts them in the log verbatim; a newline reaching the wire
    // would arrive at the GUI as a line the helper never sent.
    void keepsForgedLinesOutOfMessages()
    {
        const ProtocolLine forged = parseProtocolLine(
            encodeDetailLine(QStringLiteral("mkfs failed\nresult /dev/sda\nstep 10/10 Restore complete")));
        QCOMPARE(forged.kind, ProtocolLineKind::Detail);
        QCOMPARE(forged.text, QStringLiteral("mkfs failed result /dev/sda step 10/10 Restore complete"));

        QCOMPARE(sanitizeProtocolText(QStringLiteral("a\r\n\tb")), QStringLiteral("a b"));
        QCOMPARE(sanitizeProtocolText(QStringLiteral("  padded  ")), QStringLiteral("padded"));
    }

    // A cancelled restore is not a failed one, and reporting it as an error is
    // the bug this distinction exists to prevent. Both must come back empty so
    // the worker emits cancelled() rather than failed().
    void reportsNoErrorForSuccessOrCancellation()
    {
        QVERIFY(describeRestoreExit(RestoreExitSuccess, QString()).isEmpty());
        QVERIFY(describeRestoreExit(RestoreExitCancelled, QString()).isEmpty());
        QVERIFY(describeRestoreExit(RestoreExitCancelled, QStringLiteral("ignored")).isEmpty());
    }

    void explainsHelperExitCodes()
    {
        QCOMPARE(
            describeRestoreExit(RestoreExitFailed, QStringLiteral("/dev/sdb is read-only.")),
            QStringLiteral("/dev/sdb is read-only."));

        // The helper always explains itself, so an empty stderr means it died
        // before it could rather than that nothing went wrong.
        QVERIFY(!describeRestoreExit(RestoreExitFailed, QString()).isEmpty());

        QVERIFY(describeRestoreExit(RestoreExitUsage, QString()).contains(QStringLiteral("Reinstall")));

        // pkexec's codes, which the helper never returns.
        QVERIFY(describeRestoreExit(126, QString()).contains(QStringLiteral("Authentication")));
        QVERIFY(describeRestoreExit(127, QString()).contains(QStringLiteral("installed")));

        // Anything else still has to produce something a person can act on.
        const QString unexpected = describeRestoreExit(9, QString());
        QVERIFY(!unexpected.isEmpty());
        QVERIFY(unexpected.contains(QStringLiteral("9")));
    }

    // The error line crosses the same boundary the progress lines do, and ends
    // up in a dialog and the log.
    void flattensTheHelpersErrorLine()
    {
        QCOMPARE(
            describeRestoreExit(RestoreExitFailed, QStringLiteral("mkfs failed\nresult /dev/sda")),
            QStringLiteral("mkfs failed result /dev/sda"));
    }

    void parsesProtocolLines()
    {
        const ProtocolLine version = parseProtocolLine(encodeVersionLine(RestoreProtocolVersion));
        QCOMPARE(version.kind, ProtocolLineKind::Version);
        QCOMPARE(version.version, RestoreProtocolVersion);

        const ProtocolLine step = parseProtocolLine(encodeStepLine(3, 10, QStringLiteral("Formatting as exFAT")));
        QCOMPARE(step.kind, ProtocolLineKind::Step);
        QCOMPARE(step.step, 3);
        QCOMPARE(step.totalSteps, 10);
        QCOMPARE(step.text, QStringLiteral("Formatting as exFAT"));

        const ProtocolLine result = parseProtocolLine(encodeResultLine(QStringLiteral("/dev/sdb1")));
        QCOMPARE(result.kind, ProtocolLineKind::Result);
        QCOMPARE(result.text, QStringLiteral("/dev/sdb1"));

        // Anything the reader does not recognise is dropped rather than guessed
        // at: a line it half-understands is a line it should not act on.
        QCOMPARE(parseProtocolLine(QStringLiteral("step 3 Formatting")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QStringLiteral("step three/ten Formatting")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QStringLiteral("version soon")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QStringLiteral("mkfs.exfat: not found")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QString()).kind, ProtocolLineKind::Unknown);
    }
};

QTEST_MAIN(CoreTests)
#include "test_core.moc"

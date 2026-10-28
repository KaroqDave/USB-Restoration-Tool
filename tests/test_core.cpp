#include "core/disk.h"
#include "core/partition_table.h"
#include "core/restore_protocol.h"
#include "core/restore_steps.h"
#include "core/safety.h"

#include <QtTest/QtTest>

#include <limits>

using namespace usbrestore;

namespace {

// Passes every safety check, so each test can break exactly one thing.
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
    void refusesImpossibleLayoutsBeforeRestore()
    {
        DiskInfo disk = healthyUsbDisk();
        QString error;
        QVERIFY(canCreatePartitionLayout(PartitionStyle::Mbr, disk, &error));
        disk.size = 4ull * 1024 * 1024 * 1024 * 1024;
        QVERIFY(!canCreatePartitionLayout(PartitionStyle::Mbr, disk, &error));
        QVERIFY(error.contains(QStringLiteral("GPT")));
        QVERIFY(canCreatePartitionLayout(PartitionStyle::Gpt, disk));
        disk.sectorSize = 4096;
        QVERIFY(canCreatePartitionLayout(PartitionStyle::Mbr, disk));
        QVERIFY(!canCreatePartitionLayout(PartitionStyle::Unknown, disk));
        disk.size = 1024;
        QVERIFY(!canCreatePartitionLayout(PartitionStyle::Gpt, disk));
        disk.sectorSize = 0;
        QVERIFY(!canCreatePartitionLayout(PartitionStyle::Mbr, disk));
    }

    // The GUI's preflight and the table writer share one set of layout rules, so a disk the
    // preflight accepts never reaches erasure only to be refused by the writer.
    void preflightAndTableWriterAgreeOnLayouts()
    {
        constexpr std::uint64_t MiB = 1024ull * 1024ull;
        constexpr std::uint64_t TiB = 1024ull * 1024ull * MiB;
        for (const std::uint64_t size : {MiB, 3 * MiB, 8 * MiB, 16 * 1024 * MiB, 2 * TiB, 2 * TiB + MiB, 17 * TiB}) {
            for (const quint32 sector : {0u, 512u, 999u, 4096u}) {
                for (const auto style : {PartitionStyle::Gpt, PartitionStyle::Mbr, PartitionStyle::Unknown}) {
                    DiskInfo disk = healthyUsbDisk();
                    disk.size = size;
                    disk.sectorSize = sector;
                    QCOMPARE(
                        isWritablePartitionRequest(tableRequestFor(size, sector, style)),
                        canCreatePartitionLayout(style, disk));
                }
            }
        }
    }

    void refusesReplacedOrUnverifiableDeviceInstances()
    {
        const QString device = QStringLiteral("/dev/sdb");
        QVERIFY(isSameDeviceSequence(device, 12, 12));
        QString reason;
        QVERIFY(!isSameDeviceSequence(device, 12, 13, &reason));
        QVERIFY(reason.contains(device));
        QVERIFY(!isSameDeviceSequence(device, 12, std::nullopt));
        QVERIFY(!isSameDeviceSequence(device, std::nullopt, 12));
        QVERIFY(!isSameDeviceSequence(device, std::nullopt, std::nullopt));
        QVERIFY(!isSameDeviceSequence(device, 0, 0));
        DiskInfo selected = healthyUsbDisk();
        selected.deviceSequence = 12;
        DiskInfo current = selected;
        QVERIFY(isSameRestoreTarget(selected, current));
        current.deviceSequence = 13;
        QVERIFY(!isSameRestoreTarget(selected, current));
        current.deviceSequence.reset();
        QVERIFY(!isSameRestoreTarget(selected, current));
    }

    void boundsFormatterMappingWithoutOverflow()
    {
        constexpr auto maximum = std::numeric_limits<std::uint64_t>::max();
        QVERIFY(isValidDeviceRange(4096, 512, 3584, 512));
        QVERIFY(!isValidDeviceRange(4096, 512, 4096, 512));
        QVERIFY(!isValidDeviceRange(maximum, maximum - 511, 1024, 512));
        QVERIFY(!isValidDeviceRange(4096, 1, 512, 512));
        QVERIFY(!isValidDeviceRange(4096, 0, 513, 512));
        QVERIFY(!isValidDeviceRange(4096, 0, 0, 512));
        QVERIFY(!isValidDeviceRange(4096, 0, 512, 0));
    }

    void preservesDeviceSequenceAcrossHelperArguments()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.deviceSequence = std::numeric_limits<std::uint64_t>::max();
        auto arguments = buildRestoreArguments(disk, PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB"));
        RestoreArguments parsed;
        QVERIFY(parseRestoreArguments(arguments, &parsed));
        QCOMPARE(parsed.expected.deviceSequence, disk.deviceSequence);
        const int value = arguments.indexOf(QStringLiteral("--expect-sequence")) + 1;
        for (const QString &invalid :
             {QStringLiteral("0"),
              QStringLiteral("-1"),
              QStringLiteral("+1"),
              QStringLiteral(" 12"),
              QStringLiteral("18446744073709551616")}) {
            arguments[value] = invalid;
            QVERIFY(!parseRestoreArguments(arguments, &parsed));
        }
    }

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

    // 1.0.0 let bus type 0 through as "unknown is fine"; an unreadable bus is
    // exactly the disk not to erase.
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

    // Otherwise one blank device in the guard would refuse every disk.
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

    // "/" holds "/boot" even when it is not a separate mount, so containment counts.
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

    // Taking the last field once invented a serial from an underscored product name,
    // so two identical serial-less sticks passed isSameRestoreTarget() as one disk.
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

    // The threshold is inclusive, so a 128 GiB stick is warned about and one byte smaller is not.
    void treatsTheLargeTargetThresholdAsInclusive()
    {
        DiskInfo atThreshold = healthyUsbDisk();
        atThreshold.size = LargeRestoreTargetBytes;
        QVERIFY(isLargeRestoreTarget(atThreshold));
        QVERIFY(!largeRestoreTargetWarning(atThreshold).isEmpty());

        DiskInfo justBelow = healthyUsbDisk();
        justBelow.size = LargeRestoreTargetBytes - 1ull;
        QVERIFY(!isLargeRestoreTarget(justBelow));
        QVERIFY(largeRestoreTargetWarning(justBelow).isEmpty());
    }

    // 1 MiB boundaries are what other partitioners produce and `sgdisk -v` expects.
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
        const std::uint64_t backupGpt = (gptEntryArraySectors(4096) + 1ull) * 4096ull;
        QVERIFY(layout.startOffset + layout.length + backupGpt <= 32ull * 1024ull * 1024ull * 1024ull);
    }

    // The GPT tail is 33 sectors at 512 bytes and 5 at 4096, not one constant.
    void sizesTheGptEntryArrayInSectors()
    {
        QCOMPARE(gptEntryArraySectors(512), 32u);
        QCOMPARE(gptEntryArraySectors(4096), 4u);
        // Unsupported sizes fall back to the 512 the rest of the layout assumes.
        QCOMPARE(gptEntryArraySectors(0), 32u);
        QCOMPARE(gptEntryArraySectors(999), 32u);
    }

    // The calculator and the write gate derive the backup-GPT tail separately; a
    // mismatch shows only where rounding leaves no slack, so walk the last MiB.
    void acceptsEveryLayoutTheCalculatorProduces()
    {
        constexpr std::uint64_t oneMiB = 1024ull * 1024ull;
        constexpr std::uint64_t base = 16ull * 1024ull * 1024ull * 1024ull;

        for (const quint32 sectorSize : {512u, 4096u}) {
            for (std::uint64_t sectors = 0; sectors <= oneMiB / sectorSize + 1; ++sectors) {
                const std::uint64_t diskSize = base + sectors * sectorSize;
                const auto request = tableRequestFor(diskSize, sectorSize, PartitionStyle::Gpt);
                QVERIFY(request.layout.length > 0);

                QString reason;
                if (!isWritablePartitionRequest(request, &reason)) {
                    QFAIL(qPrintable(
                        QStringLiteral("%1-byte sectors, %2 bytes: %3").arg(sectorSize).arg(diskSize).arg(reason)));
                }

                // The backup image ends on the last whole sector, not past it.
                QCOMPARE(
                    gptBackupOffset(request) + static_cast<std::uint64_t>(buildGptBackup(request).size()),
                    (diskSize / sectorSize) * static_cast<std::uint64_t>(sectorSize));
            }
        }
    }

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

    void refusesUnalignedZeroRanges()
    {
        // Aligned ranges pass, for both supported sector sizes.
        QVERIFY(unalignedZeroRangeError(0, 1024 * 1024, 512).isEmpty());
        QVERIFY(unalignedZeroRangeError(4096, 8192, 4096).isEmpty());

        // Refused, not rounded: rounding down would silently leave the tail unzeroed.
        QVERIFY(!unalignedZeroRangeError(0, 1000, 512).isEmpty());
        QVERIFY(!unalignedZeroRangeError(100, 4096, 4096).isEmpty());
        QVERIFY(!unalignedZeroRangeError(512, 512, 4096).isEmpty());

        // Unsupported sizes validate against 512, the same fallback on both platforms.
        QVERIFY(unalignedZeroRangeError(512, 1024, 600).isEmpty());
        QVERIFY(!unalignedZeroRangeError(0, 600, 600).isEmpty());
    }

    void treatsOnlyAReportedZeroOffsetAsTheDiskStart()
    {
        QVERIFY(isPartitionAtDiskStart(0));
        QVERIFY(!isPartitionAtDiskStart(512));
        QVERIFY(!isPartitionAtDiskStart(1024 * 1024));

        // A property WMI did not report must not read as 0, or a real partition skips deletion.
        QVERIFY(!isPartitionAtDiskStart(std::nullopt));
    }

    void boundsDriverEntryCountsByTheBytesReturned()
    {
        constexpr std::uint64_t header = 48;
        constexpr std::uint64_t entry = 144;
        QCOMPARE(boundedEntryCount(1, header + entry, header, entry), 1u);
        QCOMPARE(boundedEntryCount(0, header + entry * 4, header, entry), 0u);

        // A count beyond the data returned is cut to the whole entries that arrived.
        QCOMPARE(boundedEntryCount(128, header + entry * 2, header, entry), 2u);
        QCOMPARE(
            boundedEntryCount(std::numeric_limits<std::uint32_t>::max(), header + entry * 3 + 100, header, entry), 3u);

        // Fewer bytes than the header, or a zero entry size, leaves nothing to read.
        QCOMPARE(boundedEntryCount(4, header - 8, header, entry), 0u);
        QCOMPARE(boundedEntryCount(4, 1000, header, 0), 0u);
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
        // Each FAT needs only 64 sectors, but the data region needs just over 256 MiB.
        constexpr std::uint64_t minimumVolume = (32ull + 2ull * 64ull + 65527ull) * 4096ull;

        QCOMPARE(fat32AllocationUnitSize(minimumVolume - 4096, 4096, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(minimumVolume, 4096, maximumVolume), 4096u);
    }

    void growsTheFat32AllocationUnitWithTheVolume()
    {
        // format.com defaults, not the smallest legal unit: 1 GiB gets 4 KiB
        // clusters, not 512-byte ones that need a 16 MB FAT.
        constexpr std::uint64_t maximumVolume = 32ull * 1024ull * 1024ull * 1024ull;
        constexpr std::uint64_t oneGiB = 1024ull * 1024ull * 1024ull;
        QCOMPARE(fat32AllocationUnitSize(1ull * oneGiB, 512, maximumVolume), 4096u);
        QCOMPARE(fat32AllocationUnitSize(4ull * oneGiB, 512, maximumVolume), 4096u);
        QCOMPARE(fat32AllocationUnitSize(10ull * oneGiB, 512, maximumVolume), 8u * 1024u);
        QCOMPARE(fat32AllocationUnitSize(20ull * oneGiB, 512, maximumVolume), 16u * 1024u);
        QCOMPARE(fat32AllocationUnitSize(maximumVolume, 512, maximumVolume), 16u * 1024u);
    }

    void refusesFat32GeometryOutsideTheFormatterLimits()
    {
        constexpr std::uint64_t maximumVolume = 32ull * 1024ull * 1024ull * 1024ull;
        QCOMPARE(fat32AllocationUnitSize(maximumVolume + 512, 512, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(1024ull * 1024ull * 1024ull, 600, maximumVolume), 0u);
        QCOMPARE(fat32AllocationUnitSize(1024ull * 1024ull * 1024ull + 1, 512, maximumVolume), 0u);
    }

    // From dosfstools 4.2 mkfs.fat.c, checked against the binary: 1 MiB outside
    // these limits mkfs.vfat only warns, which is why they are enforced here.
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

        // Not the Windows 32 GiB limit, which would refuse volumes mkfs.vfat handles.
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

        // A 4 TB USB hard disk with 512-byte sectors is over the limit.
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
    // Only the Linux backend writes these (Windows uses an IOCTL), so a wrong
    // byte here is a disk that no longer mounts.

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

        // The header checksum covers the first 92 bytes with its own field zeroed.
        QByteArray zeroed = header.left(92);
        zeroed.replace(16, 4, QByteArray(4, '\0'));
        QCOMPARE(readLe32(header, 16), gptCrc32(zeroed));

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

        QCOMPARE(gptBackupOffset(request), (lastLba - 32) * 512);
        QCOMPARE(gptBackupOffset(request) + static_cast<std::uint64_t>(backup.size()), request.diskSize);

        QCOMPARE(backup.left(128), buildGptPrimary(request).mid(1024, 128));
    }

    void buildsAnMbrForOlderDevices()
    {
        const auto request = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Mbr);
        QVERIFY(isWritablePartitionRequest(request));

        const QByteArray mbr = buildMbr(request);
        QCOMPARE(mbr.size(), 512);
        // Non-zero signature: Windows tells disks apart by it.
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

        // Stays inside the disk but overwrites the backup GPT; the past-the-end
        // check used to let this through.
        auto overlapsBackup = tableRequestFor(16ull * 1024ull * 1024ull * 1024ull, 512, PartitionStyle::Gpt);
        overlapsBackup.layout.length = overlapsBackup.diskSize - overlapsBackup.layout.startOffset;
        QVERIFY(!isWritablePartitionRequest(overlapsBackup, &reason));
        QVERIFY(reason.contains(QStringLiteral("GPT")));
    }

    // MBR's 32-bit sector addresses run out at 2 TiB: refuse, don't truncate.
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

    // --- GUI to helper boundary: arguments come from an unprivileged process,
    // so each test is a way the helper must refuse to be talked into something.

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

        // The disk the helper enumerates itself still matches the one the GUI meant.
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

    // A DiskInfo built from arguments has no bus and so is refused: feeding one
    // to the safety check by mistake fails closed.
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

    // Safe because isSameRestoreTarget() skips an identifier the selection lacks.
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

        // Any separator would let a caller name something outside /dev.
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/../etc/shadow")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/disk/by-id/usb-SanDisk")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/etc/shadow")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/")));
        QVERIFY(!isPlausibleDeviceNodePath(QStringLiteral("/dev/sd b")));
        QVERIFY(!isPlausibleDeviceNodePath(QString()));
    }

    // mkfs parses its own argv, so a label starting with "-" would read as an option.
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

    void refusesMalformedArgumentValues_data()
    {
        QTest::addColumn<QString>("flag");
        QTest::addColumn<QString>("value");

        const auto add = [](const char *name, const char *flag, const QString &value) {
            QTest::newRow(name) << QString::fromLatin1(flag) << value;
        };

        add("device trailing slash", "--device", QStringLiteral("/dev/sdb/"));
        add("device trailing newline", "--device", QStringLiteral("/dev/sdb\n"));
        add("device trailing space", "--device", QStringLiteral("/dev/sdb "));
        add("device relative", "--device", QStringLiteral("dev/sdb"));
        add("device upper-case prefix", "--device", QStringLiteral("/DEV/sdb"));
        add("device dot segment", "--device", QStringLiteral("/dev/./sdb"));
        add("device embedded NUL", "--device", QStringLiteral("/dev/sd") + QChar(0) + QStringLiteral("b"));
        add("device non-ASCII letter", "--device", QStringLiteral("/dev/sdé"));
        add("device fullwidth letter", "--device", QStringLiteral("/dev/ｓdb"));
        add("device name too long", "--device", QStringLiteral("/dev/") + QString(65, QLatin1Char('a')));

        add("label empty", "--label", QString());
        add("label lone dash", "--label", QStringLiteral("-"));
        add("label newline", "--label", QStringLiteral("USB\n"));
        add("label tab", "--label", QStringLiteral("USB\t1"));
        add("label non-breaking space", "--label", QStringLiteral("US B"));
        add("label umlaut", "--label", QStringLiteral("ÜSB"));
        add("label zero-width space", "--label", QStringLiteral("USB​"));
        add("label twelve characters", "--label", QStringLiteral("123456789012"));

        // Only the decimal digits buildRestoreArguments() writes: a looser parse
        // would turn a garbled claim into a plausible one.
        add("size empty", "--expect-size", QString());
        add("size negative", "--expect-size", QStringLiteral("-1"));
        add("size plus sign", "--expect-size", QStringLiteral("+16"));
        add("size leading space", "--expect-size", QStringLiteral(" 16"));
        add("size trailing space", "--expect-size", QStringLiteral("16 "));
        add("size hexadecimal", "--expect-size", QStringLiteral("0x10"));
        add("size exponent", "--expect-size", QStringLiteral("1e9"));
        add("size fraction", "--expect-size", QStringLiteral("16.0"));
        add("size grouped", "--expect-size", QStringLiteral("16,000"));
        add("size Arabic-Indic digits", "--expect-size", QStringLiteral("١٦"));
        add("size past 64 bits", "--expect-size", QStringLiteral("18446744073709551616"));
        add("sector size empty", "--expect-sector-size", QString());
        add("sector size negative", "--expect-sector-size", QStringLiteral("-512"));
        add("sector size plus sign", "--expect-sector-size", QStringLiteral("+512"));
        add("sector size trailing space", "--expect-sector-size", QStringLiteral("512 "));
        add("sector size past 32 bits", "--expect-sector-size", QStringLiteral("4294967296"));
        add("sector size zero", "--expect-sector-size", QStringLiteral("0"));

        add("style upper case", "--style", QStringLiteral("GPT"));
        add("style trailing space", "--style", QStringLiteral("gpt "));
        add("style empty", "--style", QString());
        add("filesystem upper case", "--filesystem", QStringLiteral("EXFAT"));
        add("filesystem trailing newline", "--filesystem", QStringLiteral("exfat\n"));
        add("filesystem empty", "--filesystem", QString());
    }

    void refusesMalformedArgumentValues()
    {
        QFETCH(QString, flag);
        QFETCH(QString, value);

        QStringList arguments =
            buildRestoreArguments(healthyUsbDisk(), PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB"));
        const int flagIndex = arguments.indexOf(flag);
        QVERIFY(flagIndex >= 0);
        arguments[flagIndex + 1] = value;

        QString reason;
        QVERIFY(!parseRestoreArguments(arguments, nullptr, &reason));
        QVERIFY2(reason.contains(flag), qPrintable(reason));
    }

    void refusesUnrecognisedFlagSpellings_data()
    {
        QTest::addColumn<QStringList>("extra");

        QTest::newRow("upper case") << QStringList{QStringLiteral("--Device"), QStringLiteral("/dev/sdc")};
        QTest::newRow("single dash") << QStringList{QStringLiteral("-device"), QStringLiteral("/dev/sdc")};
        QTest::newRow("joined value") << QStringList{QStringLiteral("--device=/dev/sdc"), QStringLiteral("x")};
        QTest::newRow("trailing space") << QStringList{QStringLiteral("--device "), QStringLiteral("/dev/sdc")};
        QTest::newRow("empty flag") << QStringList{QString(), QStringLiteral("/dev/sdc")};
        QTest::newRow("end of options") << QStringList{QStringLiteral("--"), QStringLiteral("/dev/sdc")};
        QTest::newRow("version flag") << QStringList{QString::fromLatin1(ProtocolVersionFlag), QStringLiteral("3")};
    }

    void refusesUnrecognisedFlagSpellings()
    {
        QFETCH(QStringList, extra);

        QString reason;
        QVERIFY(!parseRestoreArguments(
            buildRestoreArguments(healthyUsbDisk(), PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB")) +
                extra,
            nullptr,
            &reason));
        QVERIFY2(reason.contains(QStringLiteral("not a recognised option")), qPrintable(reason));
    }

    // The largest values each field holds still parse, so the strict parse refuses
    // only spellings, not disks.
    void acceptsTheLargestDiskAndSectorSizes()
    {
        QStringList arguments =
            buildRestoreArguments(healthyUsbDisk(), PartitionStyle::Gpt, FileSystemType::ExFat, QStringLiteral("USB"));
        arguments[arguments.indexOf(QStringLiteral("--expect-size")) + 1] = QStringLiteral("18446744073709551615");
        arguments[arguments.indexOf(QStringLiteral("--expect-sector-size")) + 1] = QStringLiteral("4294967295");

        RestoreArguments parsed;
        QVERIFY(parseRestoreArguments(arguments, &parsed));
        QCOMPARE(parsed.expected.size, std::numeric_limits<std::uint64_t>::max());
        QCOMPARE(parsed.expected.sectorSize, std::numeric_limits<quint32>::max());
    }

    // mkfs output is logged verbatim, so a newline reaching the wire would arrive
    // at the GUI as a line the helper never sent.
    void keepsForgedLinesOutOfMessages()
    {
        const ProtocolLine forged = parseProtocolLine(
            encodeDetailLine(QStringLiteral("mkfs failed\nresult /dev/sda\nstep 10/10 Restore complete")));
        QCOMPARE(forged.kind, ProtocolLineKind::Detail);
        QCOMPARE(forged.text, QStringLiteral("mkfs failed result /dev/sda step 10/10 Restore complete"));

        QCOMPARE(sanitizeProtocolText(QStringLiteral("a\r\n\tb")), QStringLiteral("a b"));
        QCOMPARE(sanitizeProtocolText(QStringLiteral("  padded  ")), QStringLiteral("padded"));
    }

    // Both must be empty so the worker emits cancelled() rather than failed().
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

        // The helper always explains itself, so an empty stderr means it died first.
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

    // The order of the checks is the rule under test; cases marked "used to"
    // were once reported wrongly.
    void judgesHelperRunOutcomes()
    {
        const auto observe = [](bool crashed,
                                int exitCode,
                                bool sawVersion,
                                bool versionAccepted,
                                bool killRequested,
                                const QString &location) {
            HelperRunObservation run;
            run.crashed = crashed;
            run.exitCode = exitCode;
            run.sawVersion = sawVersion;
            run.versionAccepted = versionAccepted;
            run.killRequested = killRequested;
            run.location = location;
            return run;
        };

        QString error;

        // Ordinary endings: success, and a cancel honoured at a checkpoint.
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitSuccess, true, true, false, QStringLiteral("/dev/sdb1")), &error),
            HelperRunVerdict::Succeeded);
        error.clear();
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitCancelled, true, true, false, QString()), &error),
            HelperRunVerdict::Cancelled);
        QVERIFY(error.isEmpty());

        // A kill before the version line is believed only if the process died silently
        // (SIGKILL hit pkexec) or pkexec reported auth dismissed: the helper never ran.
        QCOMPARE(judgeHelperRun(observe(true, 0, false, false, true, QString()), &error), HelperRunVerdict::Cancelled);
        QVERIFY(error.isEmpty());
        QCOMPARE(
            judgeHelperRun(observe(false, 126, false, false, true, QString()), &error), HelperRunVerdict::Cancelled);
        QVERIFY(error.isEmpty());

        // pkexec had already exec'd the helper, so the kill missed and the restore
        // completed. This used to be reported as "cancelled — the disk was not changed".
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitSuccess, true, true, true, QStringLiteral("/dev/sdb1")), &error),
            HelperRunVerdict::Succeeded);
        // A late cancel token that landed at a checkpoint is a truthful cancellation.
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitCancelled, true, true, true, QString()), &error),
            HelperRunVerdict::Cancelled);

        // A rejected version outranks the exit code: its "cancelled" and "success"
        // used to be taken at face value.
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitCancelled, true, false, false, QString()), &error),
            HelperRunVerdict::Failed);
        QCOMPARE(error, describeRestoreExit(RestoreExitUsage, QString()));
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitSuccess, true, false, false, QStringLiteral("/dev/sdb1")), &error),
            HelperRunVerdict::Failed);

        // No version line: pkexec's codes keep their meaning, and a claimed success
        // or cancellation is a protocol failure.
        QCOMPARE(judgeHelperRun(observe(false, 126, false, false, false, QString()), &error), HelperRunVerdict::Failed);
        QVERIFY(error.contains(QStringLiteral("Authentication")));
        QCOMPARE(judgeHelperRun(observe(false, 127, false, false, false, QString()), &error), HelperRunVerdict::Failed);
        QVERIFY(error.contains(QStringLiteral("installed")));
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitSuccess, false, false, false, QString()), &error),
            HelperRunVerdict::Failed);
        QCOMPARE(error, describeRestoreExit(RestoreExitUsage, QString()));
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitCancelled, false, false, false, QString()), &error),
            HelperRunVerdict::Failed);
        QCOMPARE(error, describeRestoreExit(RestoreExitUsage, QString()));

        // An unrequested crash is a failure: the drive may hold a half-written table.
        QCOMPARE(judgeHelperRun(observe(true, 0, true, true, false, QString()), &error), HelperRunVerdict::Failed);
        QVERIFY(error.contains(QStringLiteral("unexpectedly")));
        // A version line means the helper ran, so this is not the pre-auth cancel.
        QCOMPARE(judgeHelperRun(observe(true, 0, true, true, true, QString()), &error), HelperRunVerdict::Failed);

        // Success without a location is refused rather than trusted.
        QCOMPARE(
            judgeHelperRun(observe(false, RestoreExitSuccess, true, true, false, QString()), &error),
            HelperRunVerdict::Failed);
        QVERIFY(error.contains(QStringLiteral("without reporting")));

        // The reason the announcement was refused is reported in place of the generic text.
        HelperRunObservation skewed = observe(false, RestoreExitCancelled, true, false, false, QString());
        skewed.versionError = QStringLiteral("helper is from version 1.4.0");
        QCOMPARE(judgeHelperRun(skewed, &error), HelperRunVerdict::Failed);
        QCOMPARE(error, skewed.versionError);
    }

    // The error line crosses the same boundary and ends up in a dialog and the log.
    void flattensTheHelpersErrorLine()
    {
        QCOMPARE(
            describeRestoreExit(RestoreExitFailed, QStringLiteral("mkfs failed\nresult /dev/sda")),
            QStringLiteral("mkfs failed result /dev/sda"));
    }

    void parsesProtocolLines()
    {
        const ProtocolLine version =
            parseProtocolLine(encodeVersionLine(RestoreProtocolVersion, QStringLiteral("1.5.1"), linuxRestorePlan()));
        QCOMPARE(version.kind, ProtocolLineKind::Version);
        QCOMPARE(version.version, RestoreProtocolVersion);
        QCOMPARE(version.appVersion, QStringLiteral("1.5.1"));
        QCOMPARE(version.totalSteps, 10);
        QCOMPARE(version.firstDestructiveStep, 5);

        const ProtocolLine step = parseProtocolLine(encodeStepLine(3, 10, QStringLiteral("Formatting as exFAT")));
        QCOMPARE(step.kind, ProtocolLineKind::Step);
        QCOMPARE(step.step, 3);
        QCOMPARE(step.totalSteps, 10);
        QCOMPARE(step.text, QStringLiteral("Formatting as exFAT"));

        const ProtocolLine result = parseProtocolLine(encodeResultLine(QStringLiteral("/dev/sdb1")));
        QCOMPARE(result.kind, ProtocolLineKind::Result);
        QCOMPARE(result.text, QStringLiteral("/dev/sdb1"));

        // A half-understood line is dropped rather than guessed at.
        QCOMPARE(parseProtocolLine(QStringLiteral("step 3 Formatting")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QStringLiteral("step three/ten Formatting")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QStringLiteral("version soon")).kind, ProtocolLineKind::Unknown);
        // A version 2 helper announced only its protocol number.
        const ProtocolLine bare = parseProtocolLine(QStringLiteral("version 2"));
        QCOMPARE(bare.kind, ProtocolLineKind::Version);
        QCOMPARE(bare.version, 2);
        QCOMPARE(bare.totalSteps, 0);
        QCOMPARE(parseProtocolLine(QStringLiteral("mkfs.exfat: not found")).kind, ProtocolLineKind::Unknown);
        QCOMPARE(parseProtocolLine(QString()).kind, ProtocolLineKind::Unknown);
    }

    void classifiesOddProtocolLines_data()
    {
        QTest::addColumn<QString>("line");
        QTest::addColumn<ProtocolLineKind>("kind");
        QTest::addColumn<QString>("text");

        const auto add = [](const char *name, const QString &line, ProtocolLineKind kind, const QString &text) {
            QTest::newRow(name) << line << kind << text;
        };

        add("keyword in upper case", QStringLiteral("DETAIL x"), ProtocolLineKind::Unknown, QString());
        add("keyword with suffix", QStringLiteral("results /dev/sdb1"), ProtocolLineKind::Unknown, QString());
        add("bare version", QStringLiteral("version"), ProtocolLineKind::Unknown, QString());
        add("version with suffix", QStringLiteral("version 3x"), ProtocolLineKind::Unknown, QString());
        add("three step counts", QStringLiteral("step 1/10/12 x"), ProtocolLineKind::Unknown, QString());
        add("no step number", QStringLiteral("step /10 x"), ProtocolLineKind::Unknown, QString());
        add("no step total", QStringLiteral("step 1/ x"), ProtocolLineKind::Unknown, QString());
        add("bare step", QStringLiteral("step"), ProtocolLineKind::Unknown, QString());
        add("leading tab", QStringLiteral("\tdetail x"), ProtocolLineKind::Detail, QStringLiteral("x"));
        add("carriage return",
            QStringLiteral("result /dev/sdb1\r"),
            ProtocolLineKind::Result,
            QStringLiteral("/dev/sdb1"));

        // What the encoders write for an empty message, trimmed by the reader.
        add("step without message", QStringLiteral("step 1/10"), ProtocolLineKind::Step, QString());
        add("bare detail", QStringLiteral("detail"), ProtocolLineKind::Detail, QString());
        add("bare result", QStringLiteral("result"), ProtocolLineKind::Result, QString());
    }

    void classifiesOddProtocolLines()
    {
        QFETCH(QString, line);
        QFETCH(ProtocolLineKind, kind);
        QFETCH(QString, text);

        const ProtocolLine parsed = parseProtocolLine(line);
        QCOMPARE(parsed.kind, kind);
        QCOMPARE(parsed.text, text);
    }

    // Each detail decides whether the GUI's unplug boundary is right, so a line
    // that names one twice or names an unknown one announces no plan at all.
    void refusesAmbiguousVersionDetails_data()
    {
        QTest::addColumn<QString>("details");

        QTest::newRow("repeated steps") << QStringLiteral("steps 10 steps 11 destructive 5");
        QTest::newRow("repeated destructive") << QStringLiteral("steps 10 destructive 5 destructive 4");
        QTest::newRow("repeated app") << QStringLiteral("app 1.6.1 app 1.6.2 steps 10 destructive 5");
        QTest::newRow("unknown key") << QStringLiteral("steps 10 destructive 5 safe 1");
        QTest::newRow("missing value") << QStringLiteral("steps 10 destructive");
        QTest::newRow("hexadecimal count") << QStringLiteral("steps 0xA destructive 5");
    }

    void refusesAmbiguousVersionDetails()
    {
        QFETCH(QString, details);

        const ProtocolLine parsed =
            parseProtocolLine(QStringLiteral("version %1 %2").arg(RestoreProtocolVersion).arg(details));
        QCOMPARE(parsed.kind, ProtocolLineKind::Version);
        QCOMPARE(parsed.version, RestoreProtocolVersion);
        QCOMPARE(parsed.totalSteps, 0);
        QCOMPARE(parsed.firstDestructiveStep, 0);
        QVERIFY(parsed.appVersion.isEmpty());
        QVERIFY(!acceptHelperAnnouncement(parsed, QStringLiteral("1.6.1"), linuxRestorePlan()));
    }

    // Every line an encoder writes must read back as that line, whatever the
    // message, after the same UTF-8 trip and trim the GUI's reader applies.
    void readsBackEveryEncodedLine_data()
    {
        QTest::addColumn<QString>("message");

        QTest::newRow("ordinary") << QStringLiteral("Formatting as exFAT");
        QTest::newRow("empty") << QString();
        QTest::newRow("only whitespace") << QStringLiteral(" \r\n\t ");
        QTest::newRow("embedded line") << QStringLiteral("failed\nresult /dev/sda");
        QTest::newRow("line separator") << QStringLiteral("a b\u0085c");
        QTest::newRow("over the limit") << QString(5000, QLatin1Char('x'));
        QTest::newRow("emoji at the limit") << QString(1999, QLatin1Char('x')) + QStringLiteral("\U0001F4BE");
    }

    void readsBackEveryEncodedLine()
    {
        QFETCH(QString, message);

        const auto overTheWire = [](const QString &line) {
            return parseProtocolLine(QString::fromUtf8(line.toUtf8()).trimmed());
        };
        const QString expected = sanitizeProtocolText(message);

        const ProtocolLine step = overTheWire(encodeStepLine(4, 10, message));
        QCOMPARE(step.kind, ProtocolLineKind::Step);
        QCOMPARE(step.step, 4);
        QCOMPARE(step.totalSteps, 10);
        QCOMPARE(step.text, expected);

        const ProtocolLine detail = overTheWire(encodeDetailLine(message));
        QCOMPARE(detail.kind, ProtocolLineKind::Detail);
        QCOMPARE(detail.text, expected);

        const ProtocolLine result = overTheWire(encodeResultLine(message));
        QCOMPARE(result.kind, ProtocolLineKind::Result);
        QCOMPARE(result.text, expected);
    }

    // Cutting a surrogate pair in half would leave text that no longer survives
    // the UTF-8 trip to the GUI unchanged.
    void truncatesTextBetweenCharacters()
    {
        const QString floppy = QStringLiteral("\U0001F4BE");
        const QString sanitized = sanitizeProtocolText(QString(1999, QLatin1Char('x')) + floppy + floppy);
        QCOMPARE(sanitized, QString(1999, QLatin1Char('x')));

        const QString fits = sanitizeProtocolText(QString(1998, QLatin1Char('x')) + floppy + floppy);
        QCOMPARE(fits, QString(1998, QLatin1Char('x')) + floppy);
        QCOMPARE(QString::fromUtf8(fits.toUtf8()), fits);

        // Found by usb-core-fuzz: the space before the last word went in unchecked.
        QCOMPARE(
            sanitizeProtocolText(QString(1999, QLatin1Char('x')) + QStringLiteral(" y")),
            QString(1999, QLatin1Char('x')));
        QCOMPARE(
            sanitizeProtocolText(QString(1998, QLatin1Char('x')) + QStringLiteral(" y")),
            QString(1998, QLatin1Char('x')) + QStringLiteral(" y"));
        // A space and a high surrogate fit, its low half does not: neither is kept.
        QCOMPARE(
            sanitizeProtocolText(QString(1998, QLatin1Char('x')) + QLatin1Char(' ') + floppy),
            QString(1998, QLatin1Char('x')));
    }

    // The GUI tells the user unplugging is safe before firstDestructiveStep, so these
    // numbers are pinned to the steps each backend actually performs.
    void pinsTheLinuxRestorePlan()
    {
        const QVector<RestoreStep> &plan = linuxRestorePlan();
        QVERIFY(plan.indexOf(RestoreStep::Format) < plan.indexOf(RestoreStep::RereadPartitionTable));
        QCOMPARE(plan.size(), 10);
        QCOMPARE(firstDestructiveStepOf(plan), 5);
        QCOMPARE(plan.at(4), RestoreStep::ClearSignatures);
        QCOMPARE(plan.first(), RestoreStep::VerifyDisk);
        QCOMPARE(plan.last(), RestoreStep::Complete);
    }

    void pinsTheWindowsRestorePlan()
    {
        const QVector<RestoreStep> &plan = windowsRestorePlan();
        QCOMPARE(plan.size(), 14);
        QCOMPARE(firstDestructiveStepOf(plan), 5);
        QCOMPARE(plan.at(4), RestoreStep::DeletePartitions);
        QCOMPARE(plan.first(), RestoreStep::VerifyDisk);
        QCOMPARE(plan.last(), RestoreStep::Complete);
        // The last step before the boundary; it only edits the mount manager's database.
        QCOMPARE(plan.at(3), RestoreStep::DetachDriveLetters);
        QVERIFY(!changesDiskContents(RestoreStep::DetachDriveLetters));
    }

    void classifiesEveryStepThatWritesTheDisk()
    {
        for (const RestoreStep step :
             {RestoreStep::DeletePartitions,
              RestoreStep::ClearSignatures,
              RestoreStep::ResetToRaw,
              RestoreStep::WritePartitionTable,
              RestoreStep::CreatePartition,
              RestoreStep::Format}) {
            QVERIFY(changesDiskContents(step));
        }
        for (const RestoreStep step :
             {RestoreStep::VerifyDisk,
              RestoreStep::IdentifyDevice,
              RestoreStep::Unmount,
              RestoreStep::OpenExclusive,
              RestoreStep::OpenDrive,
              RestoreStep::RefreshMetadata}) {
            QVERIFY(!changesDiskContents(step));
        }
        QCOMPARE(firstDestructiveStepOf({RestoreStep::VerifyDisk, RestoreStep::Complete}), 3);
    }

    void countsStepsOnlyInPlanOrder()
    {
        const QVector<RestoreStep> &plan = linuxRestorePlan();
        RestoreStepCounter inOrder(plan);
        for (int index = 0; index < plan.size(); ++index) {
            QCOMPARE(inOrder.advance(plan.at(index)), index + 1);
        }
        // One call more than the plan has.
        QCOMPARE(inOrder.advance(RestoreStep::Complete), 0);

        RestoreStepCounter skipped(plan);
        QCOMPARE(skipped.advance(RestoreStep::VerifyDisk), 1);
        QCOMPARE(skipped.advance(RestoreStep::Unmount), 0);
        // A refused step does not advance, so the expected one is still next.
        QCOMPARE(skipped.advance(RestoreStep::IdentifyDevice), 2);
    }

    void acceptsOnlyAHelperWithTheSamePlan()
    {
        const QVector<RestoreStep> &plan = linuxRestorePlan();
        const QString ours = QStringLiteral("1.5.1");
        QString error;

        QVERIFY(acceptHelperAnnouncement(
            parseProtocolLine(encodeVersionLine(RestoreProtocolVersion, ours, plan)), ours, plan, &error));

        // Another release with an unchanged plan still works together.
        QVERIFY(acceptHelperAnnouncement(
            parseProtocolLine(encodeVersionLine(RestoreProtocolVersion, QStringLiteral("1.5.2"), plan)),
            ours,
            plan,
            &error));

        // An installed helper from before the announcement carried the plan.
        QVERIFY(!acceptHelperAnnouncement(parseProtocolLine(QStringLiteral("version 2")), ours, plan, &error));
        QVERIFY(error.contains(QStringLiteral("version 1.5.1")));
        QVERIFY(error.contains(QStringLiteral("an older version")));

        // Same protocol, one step more: the GUI's unplug boundary would be off by one.
        QVector<RestoreStep> longer = plan;
        longer.insert(1, RestoreStep::RefreshMetadata);
        QVERIFY(!acceptHelperAnnouncement(
            parseProtocolLine(encodeVersionLine(RestoreProtocolVersion, QStringLiteral("1.6.0"), longer)),
            ours,
            plan,
            &error));
        QVERIFY(error.contains(QStringLiteral("version 1.6.0")));
        QVERIFY(error.contains(QStringLiteral("Reinstall")));

        // Same step count, boundary moved.
        QVector<RestoreStep> moved = plan;
        moved.swapItemsAt(3, 4);
        QVERIFY(!acceptHelperAnnouncement(
            parseProtocolLine(encodeVersionLine(RestoreProtocolVersion, ours, moved)), ours, plan, &error));
        QVERIFY(error.contains(QStringLiteral("a different build of version 1.5.1")));

        // Malformed details are not guessed at.
        const QString badCount = QStringLiteral("version %1 app 1.5.1 steps ten destructive 5");
        QVERIFY(!acceptHelperAnnouncement(parseProtocolLine(badCount.arg(RestoreProtocolVersion)), ours, plan, &error));
        QVERIFY(!acceptHelperAnnouncement(
            parseProtocolLine(QStringLiteral("version %1 steps 10").arg(RestoreProtocolVersion)), ours, plan, &error));

        // No announcement at all.
        QVERIFY(
            !acceptHelperAnnouncement(parseProtocolLine(QStringLiteral("step 1/10 Verifying")), ours, plan, &error));
        QCOMPARE(error, describeRestoreExit(RestoreExitUsage, QString()));
        QVERIFY(!acceptHelperAnnouncement(parseProtocolLine(QString()), ours, plan, &error));
    }
};

QTEST_MAIN(CoreTests)
#include "test_core.moc"

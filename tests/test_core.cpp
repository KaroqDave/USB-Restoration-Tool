#include "core/disk.h"
#include "core/safety.h"

#include <QtTest/QtTest>

using namespace usbrestore;

namespace {

// A disk that passes every safety check, so each test can break exactly one
// thing and see that the one thing is what gets refused.
DiskInfo healthyUsbDisk()
{
    DiskInfo disk;
    disk.number = 2;
    disk.busType = UsbBusType;
    disk.name = QStringLiteral("SanDisk Ultra USB 3.0");
    disk.serialNumber = QStringLiteral("USB123");
    disk.uniqueId = QStringLiteral("unique-123");
    disk.path = QStringLiteral("\\\\?\\usbstor#disk&ven_sandisk");
    disk.size = 16ull * 1024ull * 1024ull * 1024ull;
    disk.sectorSize = 512;
    disk.driveLetters = {QStringLiteral("E:")};
    return disk;
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
        QCOMPARE(healthStatusName(static_cast<quint16>(HealthStatus::Healthy)), QStringLiteral("Healthy"));
        QCOMPARE(busTypeName(UsbBusType), QStringLiteral("USB"));
        QCOMPARE(busTypeName(17), QStringLiteral("NVMe"));
    }

    void buildsConfirmationPhrase()
    {
        QCOMPARE(confirmationPhrase(4), QStringLiteral("RESTORE DISK 4"));
    }

    void choosesFirstAvailableDriveLetterAfterC()
    {
        const quint32 mask = (1u << 0) | (1u << 1) | (1u << 2) | (1u << 3);
        QCOMPARE(firstAvailableDriveLetter(mask), QStringLiteral("E:\\"));
    }

    void neverOffersCEvenWhenFree()
    {
        QCOMPARE(firstAvailableDriveLetter(0), QStringLiteral("D:\\"));
    }

    void acceptsOrdinaryUsbDisk()
    {
        QVERIFY(isSafeRestoreTarget(healthyUsbDisk()));
    }

    void refusesNonUsbBus()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.busType = 11; // SATA
        QString reason;
        QVERIFY(!isSafeRestoreTarget(disk, &reason));
        QVERIFY(reason.contains(QStringLiteral("SATA")));
    }

    // The earlier safety check let a bus type of 0 through as "unknown is
    // fine". A disk whose bus cannot be read is exactly the disk not to erase.
    void refusesUnknownBus()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.busType = 0;
        QVERIFY(!isSafeRestoreTarget(disk));
    }

    void refusesProtectedDriveLetter()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.driveLetters = {QStringLiteral("C:")};
        QString reason;
        QVERIFY(!isSafeRestoreTarget(disk, &reason));
        QVERIFY(reason.contains(QStringLiteral("protected")));
    }

    void refusesCallerSuppliedProtectedLetter()
    {
        DiskInfo disk = healthyUsbDisk();
        disk.driveLetters = {QStringLiteral("E:")};
        QVERIFY(isSafeRestoreTarget(disk, nullptr, {QStringLiteral("F:")}));
        QVERIFY(!isSafeRestoreTarget(disk, nullptr, {QStringLiteral("E:")}));
        // The letter is matched however it is spelled.
        QVERIFY(!isSafeRestoreTarget(disk, nullptr, {QStringLiteral("e:\\")}));
    }

    void refusesBootSystemOfflineAndReadOnlyDisks()
    {
        QString reason;

        DiskInfo boot = healthyUsbDisk();
        boot.isBoot = true;
        QVERIFY(!isSafeRestoreTarget(boot, &reason));
        QVERIFY(reason.contains(QStringLiteral("boot")));

        DiskInfo offline = healthyUsbDisk();
        offline.isOffline = true;
        reason.clear();
        QVERIFY(!isSafeRestoreTarget(offline, &reason));
        QVERIFY(reason.contains(QStringLiteral("offline")));

        DiskInfo readOnly = healthyUsbDisk();
        readOnly.isReadOnly = true;
        reason.clear();
        QVERIFY(!isSafeRestoreTarget(readOnly, &reason));
        QVERIFY(reason.contains(QStringLiteral("read-only")));
    }

    void refusesEmptyTinyAndOddlySectoredDisks()
    {
        DiskInfo empty = healthyUsbDisk();
        empty.size = 0;
        QVERIFY(!isSafeRestoreTarget(empty));

        DiskInfo tiny = healthyUsbDisk();
        tiny.size = 4ull * 1024ull * 1024ull;
        QVERIFY(!isSafeRestoreTarget(tiny));

        DiskInfo oddSectors = healthyUsbDisk();
        oddSectors.sectorSize = 511;
        QString reason;
        QVERIFY(!isSafeRestoreTarget(oddSectors, &reason));
        QVERIFY(reason.contains(QStringLiteral("sector size")));
    }

    void acceptsUnchangedRestoreTarget()
    {
        const DiskInfo selected = healthyUsbDisk();
        QVERIFY(isSameRestoreTarget(selected, selected));
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

    // The earlier check stopped at the first strong identifier that matched, so
    // a device keeping its serial number while changing its path slipped past.
    void rejectsChangedPathEvenWhenSerialMatches()
    {
        const DiskInfo selected = healthyUsbDisk();
        DiskInfo current = selected;
        current.path = QStringLiteral("\\\\?\\usbstor#disk&ven_other");

        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, current, &reason));
        QVERIFY(reason.contains(QStringLiteral("device path")));
    }

    void ignoresIdentifiersOnlyOneSideReports()
    {
        DiskInfo selected = healthyUsbDisk();
        selected.uniqueId.clear();
        const DiskInfo current = healthyUsbDisk();
        QVERIFY(isSameRestoreTarget(selected, current));
    }

    void rejectsChangedSizeAndNumber()
    {
        const DiskInfo selected = healthyUsbDisk();

        DiskInfo resized = selected;
        resized.size += 512;
        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, resized, &reason));
        QVERIFY(reason.contains(QStringLiteral("size")));

        DiskInfo renumbered = selected;
        renumbered.number = 3;
        reason.clear();
        QVERIFY(!isSameRestoreTarget(selected, renumbered, &reason));
        QVERIFY(reason.contains(QStringLiteral("disk number")));
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

    void calculatesAlignedGptLayout()
    {
        const GptLayout layout = calculateGptLayout(16ull * 1024ull * 1024ull * 1024ull, 512);
        QCOMPARE(layout.startOffset, 1024ull * 1024ull);
        QVERIFY(layout.length > 15ull * 1024ull * 1024ull * 1024ull);
        QCOMPARE(layout.length % 512, 0ull);
        // The partition plus its 1 MiB head start and the backup GPT must fit.
        QVERIFY(layout.startOffset + layout.length + 33ull * 512ull <= 16ull * 1024ull * 1024ull * 1024ull);
    }

    void alignsGptLayoutToLargeSectors()
    {
        const GptLayout layout = calculateGptLayout(32ull * 1024ull * 1024ull * 1024ull, 4096);
        QCOMPARE(layout.startOffset % 4096ull, 0ull);
        QCOMPARE(layout.length % 4096ull, 0ull);
        QVERIFY(layout.startOffset + layout.length + 33ull * 4096ull <= 32ull * 1024ull * 1024ull * 1024ull);
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

    void describesDiskForPrompts()
    {
        const QString description = describeDisk(healthyUsbDisk());
        QVERIFY(description.contains(QStringLiteral("Disk 2")));
        QVERIFY(description.contains(QStringLiteral("SanDisk")));
        QVERIFY(description.contains(QStringLiteral("16.00 GB")));
    }
};

QTEST_MAIN(CoreTests)
#include "test_core.moc"

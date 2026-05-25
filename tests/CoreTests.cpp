#include "win/Core.h"

#include <QtTest/QtTest>

using namespace usbrestore;

class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void formatsByteSizes()
    {
        QCOMPARE(formatByteSize(512), QStringLiteral("512 B"));
        QCOMPARE(formatByteSize(16ull * 1024ull * 1024ull * 1024ull), QStringLiteral("16.00 GB"));
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

    void refusesProtectedDisk()
    {
        DiskInfo disk;
        disk.number = 1;
        disk.size = 1024;
        disk.driveLetters = {QStringLiteral("C:")};
        QString reason;
        QVERIFY(!isSafeRestoreTarget(disk, &reason));
        QVERIFY(reason.contains(QStringLiteral("C:")));
    }

    void acceptsOrdinaryUsbDisk()
    {
        DiskInfo disk;
        disk.number = 2;
        disk.busType = 7;
        disk.size = 16ull * 1024ull * 1024ull * 1024ull;
        disk.driveLetters = {QStringLiteral("E:")};
        QVERIFY(isSafeRestoreTarget(disk));
    }

    void refusesOfflineOrReadOnlyDisk()
    {
        DiskInfo offline;
        offline.number = 2;
        offline.busType = 7;
        offline.size = 1024;
        offline.isOffline = true;
        QString reason;
        QVERIFY(!isSafeRestoreTarget(offline, &reason));
        QVERIFY(reason.contains(QStringLiteral("offline")));

        DiskInfo readOnly;
        readOnly.number = 3;
        readOnly.busType = 7;
        readOnly.size = 1024;
        readOnly.isReadOnly = true;
        reason.clear();
        QVERIFY(!isSafeRestoreTarget(readOnly, &reason));
        QVERIFY(reason.contains(QStringLiteral("read-only")));
    }

    void rejectsStaleRestoreTarget()
    {
        DiskInfo selected;
        selected.number = 2;
        selected.size = 1024;
        selected.sectorSize = 512;
        selected.serialNumber = QStringLiteral("original");

        DiskInfo current = selected;
        current.serialNumber = QStringLiteral("replacement");

        QString reason;
        QVERIFY(!isSameRestoreTarget(selected, current, &reason));
        QVERIFY(reason.contains(QStringLiteral("serial")));
    }

    void acceptsSameRestoreTargetBySerial()
    {
        DiskInfo selected;
        selected.number = 2;
        selected.size = 1024;
        selected.sectorSize = 512;
        selected.serialNumber = QStringLiteral("USB123");
        selected.path = QStringLiteral("old path");

        DiskInfo current = selected;
        current.path = QStringLiteral("new path");

        QVERIFY(isSameRestoreTarget(selected, current));
    }

    void identifiesLargeRestoreTarget()
    {
        DiskInfo small;
        small.size = 64ull * 1024ull * 1024ull * 1024ull;
        QVERIFY(!isLargeRestoreTarget(small));

        DiskInfo large;
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
    }
};

QTEST_MAIN(CoreTests)
#include "CoreTests.moc"

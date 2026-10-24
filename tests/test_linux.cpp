#include "linux/loop_device.h"
#include "linux/linux_restore.h"
#include "linux/block_device.h"
#include "core/partition_table.h"
#include "core/safety.h"

#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest/QtTest>

#include <fcntl.h>
#include <linux/fs.h>
#include <linux/loop.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <cerrno>

using namespace usbrestore;

namespace {
constexpr qint64 MiB = 1024 * 1024;
struct Reporter : RestoreReporter {
    void step(const QString &) override {}
    void detail(const QString &) override {}
    bool cancelRequested() const override { return false; }
};

bool loopIsDetached(const QString &path)
{
    BlockDevice device(path);
    if (!device.open(BlockDevice::Access::Inspect)) {
        return false;
    }
    struct loop_info64 info = {};
    return ::ioctl(device.descriptor(), LOOP_GET_STATUS64, &info) < 0 && errno == ENXIO;
}

QString systemTool(const QString &name)
{
    for (const auto &directory : {QStringLiteral("/usr/sbin/"), QStringLiteral("/sbin/")}) {
        if (QFile::exists(directory + name)) {
            return directory + name;
        }
    }
    return {};
}

// Stands in for the USB disk: LoopDevice never scans partitions, but a restore relies on the scan.
int attachScanningLoop(int backing, int sectorSize, QString *path)
{
    const int control = ::open("/dev/loop-control", O_RDWR | O_CLOEXEC);
    const int number = control < 0 ? -1 : ::ioctl(control, LOOP_CTL_GET_FREE);
    if (control >= 0) {
        ::close(control);
    }
    if (number < 0) {
        return -1;
    }
    *path = QStringLiteral("/dev/loop%1").arg(number);
    const int fd = ::open(path->toLocal8Bit().constData(), O_RDWR | O_CLOEXEC);
    struct loop_config config = {};
    config.fd = static_cast<quint32>(backing);
    config.block_size = static_cast<quint32>(sectorSize);
    config.info.lo_flags = LO_FLAGS_PARTSCAN | LO_FLAGS_AUTOCLEAR;
    if (fd >= 0 && ::ioctl(fd, LOOP_CONFIGURE, &config) != 0) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// In 512-byte units whatever the sector size, as sysfs reports it.
quint64 firstPartitionStart(const QString &diskPath)
{
    const QString name = QFileInfo(diskPath).fileName();
    QFile start(QStringLiteral("/sys/block/%1/%1p1/start").arg(name));
    return start.open(QIODevice::ReadOnly) ? start.readAll().trimmed().toULongLong() : 0;
}

PartitionTableRequest mbrTable(qint64 diskSize, int sectorSize, const GptLayout &layout)
{
    PartitionTableRequest table;
    table.style = PartitionStyle::Mbr;
    table.diskSize = static_cast<std::uint64_t>(diskSize);
    table.sectorSize = static_cast<quint32>(sectorSize);
    table.layout = layout;
    table.diskGuid = QByteArray(16, '\x5a');
    return table;
}
} // namespace

class LinuxTests : public QObject {
    Q_OBJECT
  private slots:
    void rejectsInvalidBackingBeforeAllocatingLoop()
    {
        LoopDevice loop;
        QString error;
        QVERIFY(!loop.configure(-1, MiB, MiB, 512, &error));
        QVERIFY(!error.isEmpty());
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile file(dir.filePath(QStringLiteral("image")));
        QVERIFY(file.open(QIODevice::ReadWrite));
        QVERIFY(file.resize(MiB));
        QVERIFY(!loop.configure(file.handle(), MiB, MiB, 512, &error));
        QVERIFY(error.contains(QStringLiteral("outside")));
    }

    // 1.7.1 asked LOOP_CTL_GET_FREE again after an EBUSY claim and got the same node all 8 times.
    void skipsLoopNodeClaimedElsewhere()
    {
        if (!qEnvironmentVariableIsSet("USBRESTORE_TEST_LOOP")) {
            QSKIP("Set USBRESTORE_TEST_LOOP=1 and run as root for disposable-image loop tests.");
        }
        QVERIFY(::geteuid() == 0);
        const int control = ::open("/dev/loop-control", O_RDWR | O_CLOEXEC);
        QVERIFY(control >= 0);
        const int number = ::ioctl(control, LOOP_CTL_GET_FREE);
        ::close(control);
        QVERIFY(number >= 0);
        const QString busyPath = QStringLiteral("/dev/loop%1").arg(number);
        const int claim = ::open(busyPath.toLocal8Bit().constData(), O_RDWR | O_EXCL | O_CLOEXEC);
        QVERIFY(claim >= 0);
        const auto release = qScopeGuard([claim]() { ::close(claim); });

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile image(dir.filePath(QStringLiteral("image")));
        QVERIFY(image.open(QIODevice::ReadWrite));
        QVERIFY(image.resize(8 * MiB));
        QString error;
        LoopDevice loop;
        QVERIFY2(loop.configure(image.handle(), MiB, 6 * MiB, 512, &error), qPrintable(error));
        QVERIFY(!loop.path().isEmpty());
        QVERIFY(loop.path() != busyPath);
        QVERIFY2(loop.detach(&error), qPrintable(error));
        QVERIFY(loop.path().isEmpty());
    }

    void releasesMappingsOnEarlyExit_data()
    {
        QTest::addColumn<int>("stage");
        QTest::addColumn<int>("sectorSize");
        const QStringList stages = {
            QStringLiteral("cancel-before-erasure"),
            QStringLiteral("formatter-failed"),
            QStringLiteral("formatter-not-started"),
        };
        for (int stage = 0; stage < stages.size(); ++stage) {
            for (int sector : {512, 4096}) {
                QTest::newRow(qPrintable(stages[stage] + QString::number(sector))) << stage << sector;
            }
        }
    }

    void releasesMappingsOnEarlyExit()
    {
        if (!qEnvironmentVariableIsSet("USBRESTORE_TEST_LOOP")) {
            QSKIP("Set USBRESTORE_TEST_LOOP=1 and run as root for disposable-image loop tests.");
        }
        QVERIFY(::geteuid() == 0);
        QFETCH(int, stage);
        QFETCH(int, sectorSize);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile image(dir.filePath(QStringLiteral("image")));
        QVERIFY(image.open(QIODevice::ReadWrite));
        const QByteArray contents(8 * MiB, 'X');
        QCOMPARE(image.write(contents), contents.size());
        QVERIFY(image.flush());

        QString error;
        LoopDevice backing;
        QVERIFY2(backing.configure(image.handle(), 0, contents.size(), sectorSize, &error), qPrintable(error));
        QVERIFY2(backing.prepareForFormatting(&error), qPrintable(error));
        QString formatterPath;
        const auto leaveRestoreScope = [&]() {
            BlockDevice disk(backing.path());
            if (!disk.open(BlockDevice::Access::Exclusive, &error)) {
                return false;
            }
            LoopDevice formatter;
            if (!formatter.configure(disk.descriptor(), MiB, 6 * MiB, sectorSize, &error)) {
                return false;
            }
            formatterPath = formatter.path();
            // Cancellation exits with the old contents intact and the formatter claim still held.
            if (stage == 0) {
                return true;
            }
            if (!formatter.prepareForFormatting(&error)) {
                return false;
            }
            Reporter reporter;
            const QString tool =
                stage == 1 ? QStringLiteral("/bin/false") : dir.filePath(QStringLiteral("missing-mkfs"));
            QString formatError;
            const bool formatted = formatLinuxPartition(
                FileSystemType::ExFat,
                tool,
                formatter.path(),
                QStringLiteral("USB"),
                MiB / sectorSize,
                reporter,
                &formatError);
            error = formatError;
            return !formatted &&
                   formatError.contains(stage == 1 ? QStringLiteral("failed") : QStringLiteral("Could not run"));
        };
        QVERIFY2(leaveRestoreScope(), qPrintable(error));

        QTRY_VERIFY_WITH_TIMEOUT(loopIsDetached(formatterPath), 3000);
        for (const QString &path : {formatterPath, backing.path()}) {
            BlockDevice reclaimed(path);
            QTRY_VERIFY_WITH_TIMEOUT(reclaimed.open(BlockDevice::Access::Exclusive, &error), 3000);
        }
        QVERIFY2(backing.detach(&error), qPrintable(error));
        QVERIFY(image.seek(0));
        QCOMPARE(image.readAll(), contents);
    }

    void formatsPinnedImageDespitePathReplacement_data()
    {
        QTest::addColumn<int>("filesystem");
        QTest::addColumn<int>("sectorSize");
        QTest::addColumn<bool>("blockBacking");
        for (const auto type :
             {FileSystemType::ExFat, FileSystemType::Fat32, FileSystemType::Ntfs, FileSystemType::Ext4}) {
            for (int sector : {512, 4096}) {
                for (bool block : {false, true}) {
                    QTest::newRow(qPrintable(
                        fileSystemTypeToken(type) + QString::number(sector) +
                        (block ? QStringLiteral("-block") : QStringLiteral("-file"))))
                        << int(type) << sector << block;
                }
            }
        }
    }

    void formatsPinnedImageDespitePathReplacement()
    {
        if (!qEnvironmentVariableIsSet("USBRESTORE_TEST_LOOP")) {
            QSKIP("Set USBRESTORE_TEST_LOOP=1 and run as root for disposable-image loop tests.");
        }
        QVERIFY(::geteuid() == 0);
        QFETCH(int, filesystem);
        QFETCH(int, sectorSize);
        QFETCH(bool, blockBacking);
        const auto type = static_cast<FileSystemType>(filesystem);
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("image"));
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadWrite));
        QVERIFY(original.resize(514 * MiB));
        const QByteArray marker("UNTOUCHED");
        QCOMPARE(original.write(marker), marker.size());
        QVERIFY(original.seek(513 * MiB));
        QCOMPARE(original.write(marker), marker.size());
        QVERIFY(original.flush());

        QString error;
        LoopDevice backingLoop;
        if (blockBacking) {
            QVERIFY2(backingLoop.configure(original.handle(), 0, 514 * MiB, sectorSize, &error), qPrintable(error));
            QVERIFY2(backingLoop.prepareForFormatting(&error), qPrintable(error));
        }
        BlockDevice backingBlock(backingLoop.path());
        if (blockBacking) {
            QVERIFY2(backingBlock.open(BlockDevice::Access::Exclusive, &error), qPrintable(error));
        }
        LoopDevice loop;
        const int backingFd = blockBacking ? backingBlock.descriptor() : original.handle();
        QVERIFY2(loop.configure(backingFd, MiB, 512 * MiB, sectorSize, &error), qPrintable(error));
        const QByteArray loopPath = loop.path().toLocal8Bit();
        const int otherClaim = ::open(loopPath.constData(), O_RDWR | O_EXCL | O_CLOEXEC);
        if (otherClaim >= 0) {
            ::close(otherClaim);
        }
        QCOMPARE(otherClaim, -1);
        // Replace the original pathname after verification; only its old open file may be written.
        QVERIFY(QFile::rename(path, dir.filePath(QStringLiteral("selected-image"))));
        QFile replacement(path);
        QVERIFY(replacement.open(QIODevice::ReadWrite));
        QCOMPARE(replacement.write(marker), marker.size());
        QVERIFY(replacement.flush());
        QVERIFY2(loop.prepareForFormatting(&error), qPrintable(error));
        const int fd = ::open(loopPath.constData(), O_RDONLY | O_CLOEXEC);
        QVERIFY(fd >= 0);
        int actualSector = 0;
        const int geometry = ::ioctl(fd, BLKSSZGET, &actualSector);
        ::close(fd);
        QCOMPARE(geometry, 0);
        QCOMPARE(actualSector, sectorSize);

        const QString toolName = QStringLiteral("mkfs.%1").arg(
            type == FileSystemType::Fat32 ? QStringLiteral("fat") : fileSystemTypeToken(type));
        const QString tool = systemTool(toolName);
        QVERIFY2(!tool.isEmpty(), qPrintable(toolName));
        Reporter reporter;
        QVERIFY2(
            formatLinuxPartition(type, tool, loop.path(), QStringLiteral("USB"), MiB / sectorSize, reporter, &error),
            qPrintable(error));
        QProcess probe;
        probe.start(
            QStringLiteral("/usr/sbin/blkid"),
            {QStringLiteral("-p"),
             QStringLiteral("-o"),
             QStringLiteral("value"),
             QStringLiteral("-s"),
             QStringLiteral("TYPE"),
             loop.path()});
        QVERIFY(probe.waitForFinished(30000));
        QCOMPARE(probe.exitCode(), 0);
        QCOMPARE(
            QString::fromUtf8(probe.readAllStandardOutput()).trimmed(),
            type == FileSystemType::Fat32 ? QStringLiteral("vfat") : fileSystemTypeToken(type));
        QVERIFY2(loop.detach(&error), qPrintable(error));
        QVERIFY(loop.path().isEmpty());
        backingBlock.close();
        QVERIFY2(backingLoop.detach(&error), qPrintable(error));
        QVERIFY(original.seek(0));
        QCOMPARE(original.read(marker.size()), marker);
        QVERIFY(original.seek(513 * MiB));
        QCOMPARE(original.read(marker.size()), marker);
        QVERIFY(replacement.seek(0));
        QCOMPARE(replacement.readAll(), marker);
        QVERIFY(original.seek(MiB));
        const QByteArray boot = original.read(512);
        if (type == FileSystemType::Fat32 || type == FileSystemType::Ntfs) {
            const auto *bytes = reinterpret_cast<const unsigned char *>(boot.constData() + 28);
            const quint32 hidden =
                quint32(bytes[0]) | (quint32(bytes[1]) << 8) | (quint32(bytes[2]) << 16) | (quint32(bytes[3]) << 24);
            QCOMPARE(hidden, quint32(MiB / sectorSize));
        }
    }

    void rereadsPartitionTableAfterFormatting_data()
    {
        QTest::addColumn<int>("sectorSize");
        QTest::addColumn<bool>("formatSucceeds");
        for (int sector : {512, 4096}) {
            for (bool succeeds : {true, false}) {
                QTest::newRow(qPrintable(
                    QString::number(sector) +
                    (succeeds ? QStringLiteral("-formatted") : QStringLiteral("-mkfs-failed"))))
                    << sector << succeeds;
            }
        }
    }

    // 1.7.1 returned before BLKRRPART when mkfs failed, leaving the old partitions over the
    // new table, and checked the new partition against the sequence sampled before erasure.
    void rereadsPartitionTableAfterFormatting()
    {
        if (!qEnvironmentVariableIsSet("USBRESTORE_TEST_LOOP")) {
            QSKIP("Set USBRESTORE_TEST_LOOP=1 and run as root for disposable-image loop tests.");
        }
        QVERIFY(::geteuid() == 0);
        QFETCH(int, sectorSize);
        QFETCH(bool, formatSucceeds);
        constexpr qint64 DiskBytes = 64 * MiB;
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QFile image(dir.filePath(QStringLiteral("image")));
        QVERIFY(image.open(QIODevice::ReadWrite));
        QVERIFY(image.resize(DiskBytes));
        const QByteArray oldTable = buildMbr(mbrTable(DiskBytes, sectorSize, {2 * MiB, 2 * MiB}));
        QCOMPARE(image.write(oldTable), oldTable.size());
        QVERIFY(image.flush());

        QString diskPath;
        const int scanningFd = attachScanningLoop(image.handle(), sectorSize, &diskPath);
        QVERIFY(scanningFd >= 0);
        const auto release = qScopeGuard([scanningFd]() {
            ::ioctl(scanningFd, LOOP_CLR_FD, 0);
            ::close(scanningFd);
        });
        QTRY_COMPARE_WITH_TIMEOUT(firstPartitionStart(diskPath), quint64(2 * MiB / 512), 3000);
        // udev probing the old partition would hold it open and make BLKRRPART fail with EBUSY.
        QProcess::execute(QStringLiteral("udevadm"), {QStringLiteral("settle")});

        QString error;
        BlockDevice device(diskPath);
        QVERIFY2(device.open(BlockDevice::Access::Exclusive, &error), qPrintable(error));
        const PartitionTableRequest newTable =
            mbrTable(DiskBytes, sectorSize, calculateGptLayout(DiskBytes, static_cast<quint32>(sectorSize)));
        QVERIFY2(device.writeAt(0, buildMbr(newTable), &error), qPrintable(error));
        QVERIFY2(device.flush(&error), qPrintable(error));
        LoopDevice formatter;
        QVERIFY2(
            formatter.configure(
                device.descriptor(), newTable.layout.startOffset, newTable.layout.length, sectorSize, &error),
            qPrintable(error));
        const QString formatterPath = formatter.path();

        RestoreRequest request;
        request.disk.deviceId = diskPath;
        request.fileSystem = FileSystemType::ExFat;
        request.volumeLabel = QStringLiteral("USB");
        const QString tool = formatSucceeds ? systemTool(QStringLiteral("mkfs.exfat")) : QStringLiteral("/bin/false");
        QVERIFY(!tool.isEmpty());
        RestoreStepCounter steps(linuxRestorePlan());
        for (RestoreStep step : linuxRestorePlan()) {
            if (step == RestoreStep::Format) {
                break;
            }
            steps.advance(step);
        }
        Reporter reporter;
        std::uint64_t sequence = 0;
        const bool formatted = formatAndRereadPartitionTable(
            request,
            tool,
            newTable.layout.startOffset / static_cast<std::uint64_t>(sectorSize),
            device,
            formatter,
            steps,
            reporter,
            &sequence,
            &error);
        QCOMPARE(formatted, formatSucceeds);
        QCOMPARE(firstPartitionStart(diskPath), quint64(newTable.layout.startOffset / 512));
        QTRY_VERIFY_WITH_TIMEOUT(loopIsDetached(formatterPath), 3000);
        if (!formatSucceeds) {
            QVERIFY2(error.contains(QStringLiteral("failed")), qPrintable(error));
            return;
        }

        std::uint64_t current = 0;
        QVERIFY(device.diskSequence(&current));
        QCOMPARE(sequence, current);
        device.close();
        BlockDevice partition(diskPath + QStringLiteral("p1"));
        QTRY_VERIFY_WITH_TIMEOUT(partition.open(BlockDevice::Access::Inspect, &error), 3000);
        QVERIFY2(partition.verifyPartitionOf(QFileInfo(diskPath).fileName(), sequence, &error), qPrintable(error));
    }
};

QTEST_GUILESS_MAIN(LinuxTests)
#include "test_linux.moc"

#include "linux/loop_device.h"
#include "linux/linux_restore.h"
#include "linux/block_device.h"

#include <QFile>
#include <QProcess>
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
        QString tool;
        for (const auto &directory : {QStringLiteral("/usr/sbin/"), QStringLiteral("/sbin/")}) {
            if (QFile::exists(directory + toolName)) {
                tool = directory + toolName;
                break;
            }
        }
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
};

QTEST_GUILESS_MAIN(LinuxTests)
#include "test_linux.moc"

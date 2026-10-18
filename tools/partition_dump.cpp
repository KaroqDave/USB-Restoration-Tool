// Writes a restore's partition table into a sparse image so CI can check it against what
// sgdisk and fdisk actually accept (driven by scripts/verify-partition-tables.sh).

#include "core/disk.h"
#include "core/partition_table.h"
#include "core/safety.h"

#include <QByteArray>
#include <QFile>
#include <QString>

#include <cstdio>

using namespace usbrestore;

namespace {

void printUsage(const char *program)
{
    std::fprintf(stderr,
                 "usage: %s <disk-size-bytes> <sector-size> <gpt|mbr> <output-image> [exfat|fat32|ntfs|ext4]\n\n"
                 "Writes a sparse image of the given size with nothing on it but the\n"
                 "partition table a restore would produce. Filesystem defaults to exfat.\n",
                 program);
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 5 && argc != 6) {
        printUsage(argv[0]);
        return 2;
    }

    PartitionTableRequest request;
    request.diskSize = QByteArray(argv[1]).toULongLong();
    request.sectorSize = QByteArray(argv[2]).toUInt();

    const QByteArray style(argv[3]);
    if (style == "gpt") {
        request.style = PartitionStyle::Gpt;
    } else if (style == "mbr") {
        request.style = PartitionStyle::Mbr;
    } else {
        printUsage(argv[0]);
        return 2;
    }

    if (argc == 6) {
        if (!parseFileSystemType(QString::fromLatin1(argv[5]), &request.fileSystem)) {
            printUsage(argv[0]);
            return 2;
        }
    }

    request.layout = calculateGptLayout(request.diskSize, request.sectorSize);
    // Fixed rather than random so a failure is reproducible.
    request.diskGuid = QByteArray::fromHex("0123456789abcdef0123456789abcdef");
    request.partitionGuid = QByteArray::fromHex("fedcba9876543210fedcba9876543210");
    request.partitionName = QStringLiteral("USB");

    QString reason;
    if (!isWritablePartitionRequest(request, &reason)) {
        std::fprintf(stderr, "refused: %s\n", qPrintable(reason));
        return 1;
    }

    QFile image(QString::fromLocal8Bit(argv[4]));
    if (!image.open(QIODevice::WriteOnly)) {
        std::fprintf(stderr, "could not open %s for writing\n", argv[4]);
        return 1;
    }
    // Sparse: a 32 GiB image costs a few hundred kilobytes on disk.
    if (!image.resize(static_cast<qint64>(request.diskSize))) {
        std::fprintf(stderr, "could not size %s to %llu bytes\n", argv[4],
                     static_cast<unsigned long long>(request.diskSize));
        return 1;
    }

    if (request.style == PartitionStyle::Gpt) {
        image.seek(0);
        image.write(buildGptPrimary(request));
        image.seek(static_cast<qint64>(gptBackupOffset(request)));
        image.write(buildGptBackup(request));
    } else {
        image.seek(0);
        image.write(buildMbr(request));
    }
    image.close();

    std::printf("%s: %s %s, %u-byte sectors, partition at %llu +%llu\n",
                argv[4],
                style.constData(),
                qPrintable(fileSystemTypeToken(request.fileSystem)),
                request.sectorSize,
                static_cast<unsigned long long>(request.layout.startOffset),
                static_cast<unsigned long long>(request.layout.length));
    return 0;
}

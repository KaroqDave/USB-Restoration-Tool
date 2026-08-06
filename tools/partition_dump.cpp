// Writes the partition table this tool would put on a disk into a sparse image
// file, so it can be handed to the partitioning tools that will have to read it
// back.
//
// The unit tests check the bytes against what the specification says. This
// checks them against what sgdisk and fdisk actually accept, which is a
// different question and the one that decides whether a restored stick mounts.
// scripts/verify-partition-tables.sh drives it; CI runs that on every push.

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
                 "usage: %s <disk-size-bytes> <sector-size> <gpt|mbr> <output-image>\n\n"
                 "Writes a sparse image of the given size with nothing on it but the\n"
                 "partition table a restore would produce.\n",
                 program);
}

} // namespace

int main(int argc, char *argv[])
{
    if (argc != 5) {
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

    request.layout = calculateGptLayout(request.diskSize, request.sectorSize);
    // Fixed rather than random so the same arguments always produce the same
    // image, which is what makes a failure reproducible.
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
    // Sparse: only the table sectors are actually written, so a 32 GiB image
    // costs a few hundred kilobytes on disk.
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

    std::printf("%s: %s, %u-byte sectors, partition at %llu +%llu\n",
                argv[4],
                style.constData(),
                request.sectorSize,
                static_cast<unsigned long long>(request.layout.startOffset),
                static_cast<unsigned long long>(request.layout.length));
    return 0;
}

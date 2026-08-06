#include "core/partition_table.h"

#include <QtGlobal>

#include <array>
#include <cstring>

namespace usbrestore {

namespace {

constexpr quint32 GptEntryCount = 128;
constexpr quint32 GptEntrySize = 128;
constexpr quint32 GptHeaderSize = 92;
constexpr quint64 MbrMaxSectors = 0xFFFFFFFFull;

// Classic BIOS translation geometry. Everything past its reach is written as
// the all-ones "look at the LBA fields instead" marker, which is what fdisk
// does and what BIOS-era devices expect to see.
constexpr quint32 MbrHeads = 255;
constexpr quint32 MbrSectorsPerTrack = 63;

void writeLe16(QByteArray &buffer, int offset, quint16 value)
{
    buffer[offset] = static_cast<char>(value & 0xFF);
    buffer[offset + 1] = static_cast<char>((value >> 8) & 0xFF);
}

void writeLe32(QByteArray &buffer, int offset, quint32 value)
{
    for (int i = 0; i < 4; ++i) {
        buffer[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
}

void writeLe64(QByteArray &buffer, int offset, quint64 value)
{
    for (int i = 0; i < 8; ++i) {
        buffer[offset + i] = static_cast<char>((value >> (8 * i)) & 0xFF);
    }
}

void writeBytes(QByteArray &buffer, int offset, const QByteArray &value, int length)
{
    const int count = qMin(length, static_cast<int>(value.size()));
    for (int i = 0; i < count; ++i) {
        buffer[offset + i] = value.at(i);
    }
}

quint64 sectorCount(const PartitionTableRequest &request)
{
    const quint64 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    return request.diskSize / sector;
}

// Sectors taken by the 128-entry, 128-byte-per-entry partition array, rounded
// up to whole sectors: 32 at 512 bytes per sector, 4 at 4096.
quint32 entryArraySectors(quint32 sectorSize)
{
    const quint32 sector = sectorSize == 0 ? 512 : sectorSize;
    const quint32 bytes = GptEntryCount * GptEntrySize;
    return (bytes + sector - 1) / sector;
}

QByteArray buildEntryArray(const PartitionTableRequest &request)
{
    QByteArray entries(static_cast<int>(GptEntryCount * GptEntrySize), '\0');

    const quint64 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    const quint64 firstLba = request.layout.startOffset / sector;
    // Inclusive, as GPT defines it: the last sector that belongs to the
    // partition, not the first one after it.
    const quint64 lastLba = (request.layout.startOffset + request.layout.length) / sector - 1;

    writeBytes(entries, 0, basicDataPartitionTypeGuid(), 16);
    writeBytes(entries, 16, request.partitionGuid, 16);
    writeLe64(entries, 32, firstLba);
    writeLe64(entries, 40, lastLba);
    writeLe64(entries, 48, 0); // No attributes: an ordinary data partition.

    // 36 UTF-16LE code units, zero-padded.
    const QString name = request.partitionName.left(36);
    for (int i = 0; i < name.size(); ++i) {
        writeLe16(entries, 56 + i * 2, name.at(i).unicode());
    }

    return entries;
}

QByteArray buildHeader(const PartitionTableRequest &request, const QByteArray &entries, bool primary)
{
    const quint32 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    const quint64 sectors = sectorCount(request);
    const quint64 lastLba = sectors - 1;
    const quint32 arraySectors = entryArraySectors(sector);

    const quint64 primaryEntryLba = 2;
    const quint64 backupEntryLba = lastLba - arraySectors;
    const quint64 firstUsable = primaryEntryLba + arraySectors;
    const quint64 lastUsable = backupEntryLba - 1;

    QByteArray header(static_cast<int>(sector), '\0');
    writeBytes(header, 0, QByteArrayLiteral("EFI PART"), 8);
    writeLe32(header, 8, 0x00010000); // Revision 1.0
    writeLe32(header, 12, GptHeaderSize);
    writeLe32(header, 16, 0); // Header CRC, filled in below.
    writeLe32(header, 20, 0); // Reserved.
    writeLe64(header, 24, primary ? 1 : lastLba);
    writeLe64(header, 32, primary ? lastLba : 1);
    writeLe64(header, 40, firstUsable);
    writeLe64(header, 48, lastUsable);
    writeBytes(header, 56, request.diskGuid, 16);
    writeLe64(header, 72, primary ? primaryEntryLba : backupEntryLba);
    writeLe32(header, 80, GptEntryCount);
    writeLe32(header, 84, GptEntrySize);
    writeLe32(header, 88, gptCrc32(entries));

    // The header checksum covers exactly the first HeaderSize bytes, with the
    // checksum field itself read as zero — which it still is at this point.
    const quint32 headerCrc = gptCrc32(header.left(static_cast<int>(GptHeaderSize)));
    writeLe32(header, 16, headerCrc);

    return header;
}

// CHS for a given LBA under the 255/63 translation, or the all-ones marker
// once the LBA no longer fits.
void writeChs(QByteArray &buffer, int offset, quint64 lba)
{
    const quint64 cylinder = lba / (MbrHeads * MbrSectorsPerTrack);
    if (cylinder > 1023) {
        buffer[offset] = static_cast<char>(0xFE);
        buffer[offset + 1] = static_cast<char>(0xFF);
        buffer[offset + 2] = static_cast<char>(0xFF);
        return;
    }

    const quint64 head = (lba / MbrSectorsPerTrack) % MbrHeads;
    const quint64 sector = (lba % MbrSectorsPerTrack) + 1;
    buffer[offset] = static_cast<char>(head & 0xFF);
    buffer[offset + 1] = static_cast<char>((sector & 0x3F) | ((cylinder >> 2) & 0xC0));
    buffer[offset + 2] = static_cast<char>(cylinder & 0xFF);
}

void writeMbrSignature(QByteArray &buffer)
{
    buffer[510] = static_cast<char>(0x55);
    buffer[511] = static_cast<char>(0xAA);
}

} // namespace

quint32 gptCrc32(const QByteArray &data)
{
    static const std::array<quint32, 256> table = [] {
        std::array<quint32, 256> generated{};
        for (quint32 i = 0; i < 256; ++i) {
            quint32 value = i;
            for (int bit = 0; bit < 8; ++bit) {
                value = (value & 1) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            }
            generated[i] = value;
        }
        return generated;
    }();

    quint32 crc = 0xFFFFFFFFu;
    for (const char byte : data) {
        crc = table[(crc ^ static_cast<quint8>(byte)) & 0xFF] ^ (crc >> 8);
    }
    return crc ^ 0xFFFFFFFFu;
}

QByteArray basicDataPartitionTypeGuid()
{
    // EBD0A0A2-B9E5-4433-87C0-68B6B72699C7. The first three groups are stored
    // little-endian and the last two as written, which is the mixed-endian
    // layout GPT inherited from Microsoft's GUID struct.
    static const char bytes[16] = {
        static_cast<char>(0xA2),
        static_cast<char>(0xA0),
        static_cast<char>(0xD0),
        static_cast<char>(0xEB),
        static_cast<char>(0xE5),
        static_cast<char>(0xB9),
        static_cast<char>(0x33),
        static_cast<char>(0x44),
        static_cast<char>(0x87),
        static_cast<char>(0xC0),
        static_cast<char>(0x68),
        static_cast<char>(0xB6),
        static_cast<char>(0xB7),
        static_cast<char>(0x26),
        static_cast<char>(0x99),
        static_cast<char>(0xC7),
    };
    return QByteArray(bytes, 16);
}

bool isWritablePartitionRequest(const PartitionTableRequest &request, QString *reason)
{
    const auto refuse = [reason](const QString &text) {
        if (reason) {
            *reason = text;
        }
        return false;
    };

    if (!isSupportedSectorSize(request.sectorSize)) {
        return refuse(QStringLiteral("Unsupported sector size of %1 bytes.").arg(request.sectorSize));
    }
    if (request.layout.length == 0) {
        return refuse(QStringLiteral("The disk is too small for a partition layout."));
    }

    const quint64 sector = request.sectorSize;
    if (request.layout.startOffset % sector != 0 || request.layout.length % sector != 0) {
        return refuse(QStringLiteral("The partition layout is not aligned to the sector size."));
    }

    const quint64 sectors = sectorCount(request);
    const quint64 endLba = (request.layout.startOffset + request.layout.length) / sector;
    if (sectors == 0 || endLba > sectors) {
        return refuse(QStringLiteral("The partition layout runs past the end of the disk."));
    }

    if (request.style == PartitionStyle::Gpt) {
        const quint32 arraySectors = entryArraySectors(request.sectorSize);
        if (sectors < static_cast<quint64>(arraySectors) * 2 + 3) {
            return refuse(QStringLiteral("The disk is too small to hold both GPT headers."));
        }
        if (request.diskGuid.size() != 16 || request.partitionGuid.size() != 16) {
            return refuse(QStringLiteral("A GPT layout needs a disk and partition identifier."));
        }
        return true;
    }

    if (request.style == PartitionStyle::Mbr) {
        // MBR addresses sectors in 32 bits. On a 512-byte-sector disk that is
        // 2 TiB, which no USB stick reaches but an external drive can.
        if (endLba > MbrMaxSectors) {
            return refuse(QStringLiteral("MBR cannot address a partition this far into the disk. Use GPT."));
        }
        if (request.diskGuid.size() < 4) {
            return refuse(QStringLiteral("An MBR layout needs a disk identifier."));
        }
        return true;
    }

    return refuse(QStringLiteral("No partition style was chosen."));
}

QByteArray buildGptPrimary(const PartitionTableRequest &request)
{
    const quint32 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    const quint64 sectors = sectorCount(request);
    const QByteArray entries = buildEntryArray(request);
    const QByteArray header = buildHeader(request, entries, true);

    // The protective MBR: one 0xEE partition covering the whole disk, so a tool
    // that only understands MBR sees an occupied disk rather than an empty one.
    QByteArray protectiveMbr(static_cast<int>(sector), '\0');
    protectiveMbr[446] = static_cast<char>(0x00);
    protectiveMbr[447] = static_cast<char>(0x00);
    protectiveMbr[448] = static_cast<char>(0x02);
    protectiveMbr[449] = static_cast<char>(0x00);
    protectiveMbr[450] = static_cast<char>(0xEE);
    protectiveMbr[451] = static_cast<char>(0xFF);
    protectiveMbr[452] = static_cast<char>(0xFF);
    protectiveMbr[453] = static_cast<char>(0xFF);
    writeLe32(protectiveMbr, 454, 1);
    writeLe32(protectiveMbr, 458, static_cast<quint32>(qMin(sectors - 1, MbrMaxSectors)));
    writeMbrSignature(protectiveMbr);

    QByteArray image;
    image.reserve(static_cast<int>(sector) * 2 + entries.size());
    image.append(protectiveMbr);
    image.append(header);
    image.append(entries);

    // The entry array is padded to whole sectors so the buffer can be written
    // in one call starting at offset 0.
    const int arrayBytes = static_cast<int>(entryArraySectors(sector) * sector);
    image.append(arrayBytes - entries.size(), '\0');
    return image;
}

QByteArray buildGptBackup(const PartitionTableRequest &request)
{
    const quint32 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    const QByteArray entries = buildEntryArray(request);
    const QByteArray header = buildHeader(request, entries, false);

    QByteArray image;
    const int arrayBytes = static_cast<int>(entryArraySectors(sector) * sector);
    image.reserve(arrayBytes + header.size());
    image.append(entries);
    image.append(arrayBytes - entries.size(), '\0');
    image.append(header);
    return image;
}

std::uint64_t gptBackupOffset(const PartitionTableRequest &request)
{
    const quint64 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    const quint64 sectors = sectorCount(request);
    const quint64 arraySectors = entryArraySectors(request.sectorSize);
    if (sectors <= arraySectors) {
        return 0;
    }
    // The array starts arraySectors before the last sector, and the backup
    // header occupies that last sector.
    return (sectors - 1 - arraySectors) * sector;
}

QByteArray buildMbr(const PartitionTableRequest &request)
{
    const quint64 sector = request.sectorSize == 0 ? 512 : request.sectorSize;
    const quint64 firstLba = request.layout.startOffset / sector;
    const quint64 sectorSpan = request.layout.length / sector;
    const quint64 lastLba = firstLba + sectorSpan - 1;

    // Sized to a whole sector, not to the 512 bytes an MBR occupies: a raw
    // write to a 4 Kn device has to be a full sector or it is rejected.
    QByteArray mbr(static_cast<int>(sector), '\0');

    // The 4-byte disk signature. Windows uses it to tell disks apart and gets
    // confused by two that share one, so leaving it as zero — which is what a
    // table written from nothing would otherwise carry — is worth avoiding.
    // The first four bytes of the disk GUID are already random.
    if (request.diskGuid.size() >= 4) {
        writeBytes(mbr, 440, request.diskGuid.left(4), 4);
    }

    mbr[446] = static_cast<char>(0x00); // Not bootable: this is a data stick.
    writeChs(mbr, 447, firstLba);
    mbr[450] = static_cast<char>(MbrExFatPartitionType);
    writeChs(mbr, 451, lastLba);
    writeLe32(mbr, 454, static_cast<quint32>(qMin(firstLba, MbrMaxSectors)));
    writeLe32(mbr, 458, static_cast<quint32>(qMin(sectorSpan, MbrMaxSectors)));
    writeMbrSignature(mbr);
    return mbr;
}

} // namespace usbrestore

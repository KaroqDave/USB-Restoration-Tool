#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace usbrestore {

// Linux has no IOCTL_DISK_SET_DRIVE_LAYOUT_EX, so the table bytes are built here;
// kept in core so the destructive path's arithmetic is testable without a disk.

QByteArray basicDataPartitionTypeGuid();

// Used for ext4 so Windows does not offer to format the stick as a RAW volume.
QByteArray linuxFilesystemPartitionTypeGuid();

QByteArray gptPartitionTypeGuid(FileSystemType fileSystem);

// 0x07 "IFS" covers exFAT and NTFS; BIOS-era devices read it to recognise the stick.
inline constexpr quint8 MbrExFatPartitionType = 0x07;
inline constexpr quint8 MbrFat32LbaPartitionType = 0x0C;
inline constexpr quint8 MbrLinuxPartitionType = 0x83;

quint8 mbrPartitionType(FileSystemType fileSystem);

struct PartitionTableRequest {
    PartitionStyle style = PartitionStyle::Gpt;
    FileSystemType fileSystem = FileSystemType::ExFat;
    std::uint64_t diskSize = 0;
    quint32 sectorSize = 512;
    GptLayout layout;
    // 16 bytes each. MBR uses the first 4 bytes of diskGuid as its disk signature.
    QByteArray diskGuid;
    QByteArray partitionGuid;
    QString partitionName;
};

// Written at disk offset 0: protective MBR, primary header, entry array.
QByteArray buildGptPrimary(const PartitionTableRequest &request);

// Entry array followed by the backup header; written at gptBackupOffset().
QByteArray buildGptBackup(const PartitionTableRequest &request);

// From the sector count, not the byte size, so a size that is not a whole number
// of sectors still puts the backup header on the last addressable sector.
std::uint64_t gptBackupOffset(const PartitionTableRequest &request);

// Padded to a whole sector so it can be written to a 4Kn device as-is.
QByteArray buildMbr(const PartitionTableRequest &request);

// Exposed so tests can verify the headers rather than trust them.
quint32 gptCrc32(const QByteArray &data);

bool isWritablePartitionRequest(const PartitionTableRequest &request, QString *reason = nullptr);

} // namespace usbrestore

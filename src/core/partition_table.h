#pragma once

#include "core/disk.h"
#include "core/safety.h"

#include <QByteArray>
#include <QString>

#include <cstdint>

namespace usbrestore {

// Byte-level serialisation of the two partition tables this tool writes.
//
// Windows does not need any of this: IOCTL_DISK_CREATE_DISK and
// IOCTL_DISK_SET_DRIVE_LAYOUT_EX describe the layout and the kernel writes the
// bytes. Linux has no such call — the partition table is just sectors on the
// disk — so the bytes are built here instead.
//
// It lives in core rather than in the Linux backend because it is pure
// arithmetic over a buffer, which makes it the one part of the destructive
// path that can be tested without a disk. Given that a wrong byte here is a
// disk that no longer mounts, that is where it belongs.

// The GUID of the GPT Basic Data Partition type,
// EBD0A0A2-B9E5-4433-87C0-68B6B72699C7, in the mixed-endian form GPT stores.
QByteArray basicDataPartitionTypeGuid();

// The GUID of the GPT Linux filesystem partition type,
// 0FC63DAF-8483-4772-8E79-3D69D8477DE4. Used for ext4 so Windows does not
// treat the stick as a RAW volume it should offer to format.
QByteArray linuxFilesystemPartitionTypeGuid();

QByteArray gptPartitionTypeGuid(FileSystemType fileSystem);

// MBR partition type 0x07: "IFS", which covers exFAT and NTFS. The type byte
// is what a BIOS-era device reads to decide whether it recognises the stick,
// so it has to say exFAT rather than "unknown".
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
    // 16 bytes each, GPT only. Callers pass random values; tests pass fixed
    // ones so the output is reproducible.
    QByteArray diskGuid;
    QByteArray partitionGuid;
    QString partitionName;
};

// A whole-disk image of everything that goes at the *start* of the disk under
// GPT: the protective MBR at LBA 0, the primary header at LBA 1, and the
// partition entry array from LBA 2. Offset 0 of the returned buffer is offset
// 0 of the disk.
QByteArray buildGptPrimary(const PartitionTableRequest &request);

// The matching tail: the partition entry array followed by the backup header,
// which occupies the last sectors of the disk.
QByteArray buildGptBackup(const PartitionTableRequest &request);

// The byte offset buildGptBackup()'s buffer is written at. Derived from the
// sector count rather than from the byte size, so a disk whose reported size
// is not a whole number of sectors still lands the backup header on the last
// addressable sector.
std::uint64_t gptBackupOffset(const PartitionTableRequest &request);

// The MBR at LBA 0, with one partition entry covering the layout and the
// 0x55AA boot signature. Returned padded to a whole sector so it can be
// written to a 4 Kn device as-is.
QByteArray buildMbr(const PartitionTableRequest &request);

// CRC-32 as GPT specifies it (the standard reflected polynomial 0xEDB88320).
// Exposed so a test can verify the headers a build produces rather than
// trusting them.
quint32 gptCrc32(const QByteArray &data);

// Whether the request describes a layout that can actually be written.
bool isWritablePartitionRequest(const PartitionTableRequest &request, QString *reason = nullptr);

} // namespace usbrestore

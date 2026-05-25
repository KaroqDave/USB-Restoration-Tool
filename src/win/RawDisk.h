#pragma once

#include "win/Core.h"

#include <QString>
#include <Windows.h>

namespace usbrestore {

class RawDisk {
public:
    explicit RawDisk(quint32 diskNumber);
    ~RawDisk();

    RawDisk(const RawDisk &) = delete;
    RawDisk &operator=(const RawDisk &) = delete;

    bool open(QString *error = nullptr);
    bool lock(QString *error = nullptr);
    bool allowExtendedIo(QString *error = nullptr);
    bool refreshLayout(QString *error = nullptr);
    bool clearPartitionSignatures(std::uint64_t diskSize, quint32 sectorSize, QString *error = nullptr);
    bool setRaw(QString *error = nullptr);
    bool createSingleGptPartition(std::uint64_t diskSize, quint32 sectorSize, QString *error = nullptr);

private:
    bool writeZeros(std::uint64_t offset, std::uint64_t bytes, QString *error);
    QString lastError(const QString &context) const;

    quint32 m_diskNumber = 0;
    HANDLE m_handle = INVALID_HANDLE_VALUE;
};

}

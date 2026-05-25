#pragma once

#include <QString>
#include <QStringList>
#include <cstdint>

namespace usbrestore {

struct GptLayout {
    std::uint64_t startOffset = 0;
    std::uint64_t length = 0;
};

struct DiskInfo {
    quint32 number = 0;
    quint32 busType = 0;
    QString name;
    QString uniqueId;
    QString serialNumber;
    QString path;
    std::uint64_t size = 0;
    quint32 sectorSize = 512;
    QString health;
    QString partitionStyle;
    QStringList driveLetters;
    QStringList labels;
    bool isBoot = false;
    bool isSystem = false;
    bool isReadOnly = false;
    bool isOffline = false;
};

QString formatByteSize(std::uint64_t bytes);
QString confirmationPhrase(quint32 diskNumber);
QString firstAvailableDriveLetter(quint32 logicalDrivesMask);
bool containsProtectedDriveLetter(const DiskInfo &disk);
bool isSafeRestoreTarget(const DiskInfo &disk, QString *reason = nullptr);
bool isSameRestoreTarget(const DiskInfo &selected, const DiskInfo &current, QString *reason = nullptr);
bool isLargeRestoreTarget(const DiskInfo &disk);
QString largeRestoreTargetWarning(const DiskInfo &disk);
GptLayout calculateGptLayout(std::uint64_t diskSize, quint32 sectorSize);

}

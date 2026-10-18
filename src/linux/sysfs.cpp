#include "linux/sysfs.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>

namespace usbrestore {

namespace {

const QString SysBlock = QStringLiteral("/sys/block");

// Virtual, optical and floppy devices can look like disks but are never USB sticks.
bool isExcludedDeviceName(const QString &name)
{
    static const QStringList prefixes = {
        QStringLiteral("loop"),
        QStringLiteral("ram"),
        QStringLiteral("dm-"),
        QStringLiteral("md"),
        QStringLiteral("zram"),
        QStringLiteral("nbd"),
        QStringLiteral("zd"),
        QStringLiteral("sr"),
        QStringLiteral("fd"),
    };
    for (const QString &prefix : prefixes) {
        if (name.startsWith(prefix)) {
            return true;
        }
    }
    return false;
}

// mountinfo escapes space, tab, newline and backslash in path fields as octal.
QString unescapeMountField(const QString &field)
{
    QString result;
    result.reserve(field.size());
    for (int i = 0; i < field.size(); ++i) {
        if (field.at(i) == QLatin1Char('\\') && i + 3 < field.size()) {
            bool ok = false;
            const int code = field.mid(i + 1, 3).toInt(&ok, 8);
            if (ok) {
                result.append(QChar(code));
                i += 3;
                continue;
            }
        }
        result.append(field.at(i));
    }
    return result;
}

DeviceNumber parseDeviceNumber(const QString &text)
{
    const QStringList parts = text.trimmed().split(QLatin1Char(':'));
    if (parts.size() != 2) {
        return {};
    }
    DeviceNumber number;
    number.major = parts.at(0).toUInt();
    number.minor = parts.at(1).toUInt();
    return number;
}

} // namespace

QString readSysfsAttribute(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

QStringList listBlockDeviceNames()
{
    QStringList names;
    const QDir dir(SysBlock);
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::System);
    for (const QString &name : entries) {
        if (!isExcludedDeviceName(name)) {
            names.append(name);
        }
    }
    names.sort();
    return names;
}

QStringList listPartitionNames(const QString &diskName)
{
    QStringList names;
    const QDir dir(QStringLiteral("%1/%2").arg(SysBlock, diskName));
    const auto entries = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        // The disk directory also holds "queue", "power" and others without this attribute.
        if (QFileInfo::exists(QStringLiteral("%1/%2/partition").arg(dir.absolutePath(), entry))) {
            names.append(entry);
        }
    }
    names.sort();
    return names;
}

DeviceNumber readDeviceNumber(const QString &sysfsPath)
{
    return parseDeviceNumber(readSysfsAttribute(QStringLiteral("%1/dev").arg(sysfsPath)));
}

QVector<MountEntry> readMounts()
{
    QVector<MountEntry> mounts;
    QFile file(QStringLiteral("/proc/self/mountinfo"));
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return mounts;
    }

    // Not a QTextStream loop: procfs files report zero bytes, so atEnd() is true at once
    // and the mount table, and with it the device guard, would come back empty.
    const QStringList lines =
        QString::fromUtf8(file.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        // Format: id parent major:minor root mountpoint options... - fstype source superopts
        const QStringList fields = line.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        if (fields.size() < 6) {
            continue;
        }

        MountEntry entry;
        entry.device = parseDeviceNumber(fields.at(2));
        entry.mountPoint = unescapeMountField(fields.at(4));
        entry.readOnly = fields.at(5).split(QLatin1Char(',')).contains(QStringLiteral("ro"));

        const int separator = fields.indexOf(QStringLiteral("-"));
        if (separator >= 0 && separator + 2 < fields.size()) {
            entry.source = unescapeMountField(fields.at(separator + 2));
        }

        mounts.append(entry);
    }
    return mounts;
}

bool isUsbBlockDevice(const QString &diskName)
{
    // e.g. .../pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host6/...
    const QFileInfo link(QStringLiteral("%1/%2").arg(SysBlock, diskName));
    const QString resolved = link.canonicalFilePath();
    if (resolved.isEmpty()) {
        return false;
    }

    // Anchored, so "usbcore" or a vendor string cannot match by accident.
    static const QRegularExpression usbController(QStringLiteral("^usb[0-9]+$"));
    for (const QString &component : resolved.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (usbController.match(component).hasMatch()) {
            return true;
        }
    }
    return false;
}

QString stableDeviceIdLink(const QString &diskName)
{
    const QDir byId(QStringLiteral("/dev/disk/by-id"));
    if (!byId.exists()) {
        return {};
    }

    const QString target = QStringLiteral("/dev/%1").arg(diskName);
    QString fallback;
    const auto entries = byId.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        const QFileInfo info(byId.absoluteFilePath(entry));
        if (info.canonicalFilePath() != target) {
            continue;
        }
        if (entry.startsWith(QStringLiteral("usb-"))) {
            return entry;
        }
        if (fallback.isEmpty()) {
            fallback = entry;
        }
    }
    return fallback;
}

QString labelForPartition(const QString &partitionName)
{
    const QDir byLabel(QStringLiteral("/dev/disk/by-label"));
    if (!byLabel.exists()) {
        return {};
    }

    const QString target = QStringLiteral("/dev/%1").arg(partitionName);
    const auto entries = byLabel.entryList(QDir::AllEntries | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &entry : entries) {
        if (QFileInfo(byLabel.absoluteFilePath(entry)).canonicalFilePath() == target) {
            // by-label names are escaped the same way mount fields are.
            return unescapeMountField(entry);
        }
    }
    return {};
}

QString parentDiskName(const QString &partitionName)
{
    const QDir dir(SysBlock);
    const auto disks = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &disk : disks) {
        if (partitionName == disk) {
            continue;
        }
        if (QFileInfo::exists(QStringLiteral("%1/%2/%3/partition").arg(SysBlock, disk, partitionName))) {
            return disk;
        }
    }
    return {};
}

namespace {

// udev's database is world-readable where /dev/sdX is not, but possibly stale, so
// only display depends on it. Values match keys by index; a missing key stays empty.
QList<QByteArray> udevProperties(const DeviceNumber &number, const QList<QByteArray> &keys)
{
    QList<QByteArray> values(keys.size());
    if (!number.isValid()) {
        return values;
    }

    QFile database(QStringLiteral("/run/udev/data/b%1:%2").arg(number.major).arg(number.minor));
    if (!database.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return values;
    }

    while (!database.atEnd()) {
        const QByteArray line = database.readLine().trimmed();
        for (qsizetype i = 0; i < keys.size(); ++i) {
            const QByteArray prefix = QByteArrayLiteral("E:") + keys.at(i) + QByteArrayLiteral("=");
            if (line.startsWith(prefix)) {
                values[i] = line.mid(prefix.size());
                break;
            }
        }
    }
    return values;
}

QByteArray udevProperty(const DeviceNumber &number, const QByteArray &key)
{
    return udevProperties(number, {key}).first();
}

// Only ever consulted when the authoritative read is unavailable.
PartitionStyle partitionStyleFromUdevDatabase(const QString &devicePath)
{
    const DeviceNumber number =
        readDeviceNumber(QStringLiteral("%1/%2").arg(SysBlock, QFileInfo(devicePath).fileName()));

    // udev spells MBR "dos", the same name fdisk uses for it.
    const QByteArray value = udevProperty(number, QByteArrayLiteral("ID_PART_TABLE_TYPE"));
    if (value == QByteArrayLiteral("gpt")) {
        return PartitionStyle::Gpt;
    }
    if (value == QByteArrayLiteral("dos")) {
        return PartitionStyle::Mbr;
    }
    return PartitionStyle::Unknown;
}

} // namespace

QString fileSystemTypeForDevice(const QString &sysfsPath)
{
    const DeviceNumber number = readDeviceNumber(sysfsPath);
    const QList<QByteArray> values = udevProperties(
        number, {QByteArrayLiteral("ID_FS_TYPE"), QByteArrayLiteral("ID_FS_VERSION")});
    const QByteArray &type = values.at(0);
    const QByteArray &version = values.at(1);

    // "vfat" covers the whole FAT family; the variant the user knows is recorded separately.
    if (type == QByteArrayLiteral("vfat") && !version.isEmpty()) {
        return QString::fromLatin1(version);
    }
    return QString::fromLatin1(type);
}

PartitionStyle detectPartitionStyle(const QString &devicePath, quint32 sectorSize)
{
    QFile device(devicePath);
    if (!device.open(QIODevice::ReadOnly)) {
        return partitionStyleFromUdevDatabase(devicePath);
    }

    const qint64 sector = sectorSize == 0 ? 512 : sectorSize;
    const QByteArray first = device.read(sector);
    if (first.size() < 512) {
        return PartitionStyle::Unknown;
    }

    // A GPT disk also has a valid protective MBR, so GPT must be checked first.
    if (device.seek(sector)) {
        const QByteArray second = device.read(8);
        if (second == QByteArrayLiteral("EFI PART")) {
            return PartitionStyle::Gpt;
        }
    }

    if (static_cast<quint8>(first.at(510)) == 0x55 && static_cast<quint8>(first.at(511)) == 0xAA) {
        return PartitionStyle::Mbr;
    }

    return PartitionStyle::Unknown;
}

} // namespace usbrestore

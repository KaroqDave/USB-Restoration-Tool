#include "linux/sysfs.h"

#include <QByteArray>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

namespace usbrestore {

namespace {

const QString SysBlock = QStringLiteral("/sys/block");

// Virtual and non-restorable block devices. A loop device or an LVM volume can
// look like a disk but is never a USB stick, and an optical drive is not
// something this tool has any business writing to.
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
        // A partition directory is one that carries a "partition" attribute;
        // the disk directory also holds "queue", "power" and others that do not.
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

    // readAll() rather than a QTextStream loop: procfs reports every file as
    // zero bytes, and QTextStream::atEnd() believes it, so a read loop exits
    // before reading a single line. This is why the mount table came back
    // empty — and with it every mount point and the whole device guard.
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
    // /sys/block/sdb is a symlink into /sys/devices, and the resolved path
    // spells out the bus: .../pci0000:00/0000:00:14.0/usb2/2-1/2-1:1.0/host6/...
    // A "usb" path component means the device is behind a USB controller.
    const QFileInfo link(QStringLiteral("%1/%2").arg(SysBlock, diskName));
    const QString resolved = link.canonicalFilePath();
    if (resolved.isEmpty()) {
        return false;
    }

    // A USB host controller appears as "usbN" and nothing else in a block
    // device's path does, so the match is anchored rather than a substring
    // search that "usbcore" or a vendor string could satisfy by accident.
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
        // The usb- form carries vendor, product and serial; the wwn- form is
        // just an opaque number, so it is only used if nothing better exists.
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

// What udev recorded when the device appeared. Its database is world-readable,
// which /dev/sdX is not: since the privileged work moved into the helper the
// GUI runs as an ordinary user and cannot open a block device at all. Without
// this every disk in the list would read "RAW / unknown".
//
// Second-hand and possibly stale — it describes the disk as udev last saw it —
// so it is only ever consulted when the authoritative read is unavailable, and
// nothing but the label in the list depends on it.
PartitionStyle partitionStyleFromUdevDatabase(const QString &devicePath)
{
    const DeviceNumber number =
        readDeviceNumber(QStringLiteral("%1/%2").arg(SysBlock, QFileInfo(devicePath).fileName()));
    if (!number.isValid()) {
        return PartitionStyle::Unknown;
    }

    QFile database(QStringLiteral("/run/udev/data/b%1:%2").arg(number.major).arg(number.minor));
    if (!database.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return PartitionStyle::Unknown;
    }

    // Property lines are "E:KEY=value". udev spells MBR "dos", the same name
    // fdisk uses for it.
    static const QByteArray key = QByteArrayLiteral("E:ID_PART_TABLE_TYPE=");
    while (!database.atEnd()) {
        const QByteArray line = database.readLine().trimmed();
        if (!line.startsWith(key)) {
            continue;
        }
        const QByteArray value = line.mid(key.size());
        if (value == QByteArrayLiteral("gpt")) {
            return PartitionStyle::Gpt;
        }
        if (value == QByteArrayLiteral("dos")) {
            return PartitionStyle::Mbr;
        }
        return PartitionStyle::Unknown;
    }
    return PartitionStyle::Unknown;
}

} // namespace

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

    // GPT's header lives in the sector after the protective MBR, so a GPT disk
    // also has a valid-looking MBR. Check for GPT first or every GPT disk
    // reports as MBR.
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

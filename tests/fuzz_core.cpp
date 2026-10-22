// Feeds arbitrary bytes to the parsers on the helper's trust boundary and to the
// partition-table builder, and checks the properties the unit tests rely on.
// Built against libFuzzer with USBRESTORE_FUZZ; otherwise main() below replays
// the files it is given, which is how ctest runs tests/fuzz-corpus everywhere.

#include "core/disk.h"
#include "core/partition_table.h"
#include "core/restore_protocol.h"
#include "core/restore_steps.h"
#include "core/safety.h"

#include <QByteArray>
#include <QString>
#include <QStringList>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

using namespace usbrestore;

namespace {

[[noreturn]] void violated(const char *condition, int line)
{
    std::fprintf(stderr, "fuzz_core.cpp:%d: invariant violated: %s\n", line, condition);
    std::abort();
}

#define FUZZ_REQUIRE(condition)                                                                                        \
    do {                                                                                                               \
        if (!(condition)) {                                                                                            \
            violated(#condition, __LINE__);                                                                            \
        }                                                                                                              \
    } while (false)

class Input {
  public:
    Input(const std::uint8_t *data, std::size_t size) : m_data(data), m_size(size) {}

    template <typename T> T take()
    {
        T value{};
        const std::size_t count = m_size < sizeof(T) ? m_size : sizeof(T);
        std::memcpy(&value, m_data, count);
        m_data += count;
        m_size -= count;
        return value;
    }

    QByteArray rest() const
    {
        return QByteArray(reinterpret_cast<const char *>(m_data), static_cast<qsizetype>(m_size));
    }

  private:
    const std::uint8_t *m_data;
    std::size_t m_size;
};

// What the GUI's reader does to a line between the helper's stdout and the parser.
ProtocolLine overTheWire(const QString &line)
{
    return parseProtocolLine(QString::fromUtf8(line.toUtf8()).trimmed());
}

void checkSanitized(const QString &text)
{
    const QString sanitized = sanitizeProtocolText(text);
    FUZZ_REQUIRE(sanitized.size() <= 2000);
    FUZZ_REQUIRE(sanitizeProtocolText(sanitized) == sanitized);
    FUZZ_REQUIRE(QString::fromUtf8(sanitized.toUtf8()) == sanitized);
    FUZZ_REQUIRE(!sanitized.startsWith(QLatin1Char(' ')) && !sanitized.endsWith(QLatin1Char(' ')));
    FUZZ_REQUIRE(!sanitized.contains(QStringLiteral("  ")));
    for (const QChar character : sanitized) {
        FUZZ_REQUIRE(character == QLatin1Char(' ') || !character.isSpace());
        FUZZ_REQUIRE(character.category() != QChar::Other_Control);
    }
}

void fuzzProtocolLines(Input &input)
{
    const int number = input.take<std::int32_t>();
    const int total = input.take<std::int32_t>();
    const bool windowsPlan = (input.take<std::uint8_t>() & 1) != 0;
    const QString text = QString::fromUtf8(input.rest());

    // As a raw line from a helper: anything may come back, but nothing may crash.
    const ProtocolLine raw = parseProtocolLine(text);
    if (raw.kind == ProtocolLineKind::Unknown) {
        FUZZ_REQUIRE(raw.text.isEmpty());
    }
    checkSanitized(text);

    // As a message: whatever an encoder writes must read back as itself.
    const QString expected = sanitizeProtocolText(text);

    const ProtocolLine step = overTheWire(encodeStepLine(number, total, text));
    FUZZ_REQUIRE(step.kind == ProtocolLineKind::Step);
    FUZZ_REQUIRE(step.step == number && step.totalSteps == total);
    FUZZ_REQUIRE(step.text == expected);

    const ProtocolLine detail = overTheWire(encodeDetailLine(text));
    FUZZ_REQUIRE(detail.kind == ProtocolLineKind::Detail && detail.text == expected);

    const ProtocolLine result = overTheWire(encodeResultLine(text));
    FUZZ_REQUIRE(result.kind == ProtocolLineKind::Result && result.text == expected);

    const QVector<RestoreStep> &plan = windowsPlan ? windowsRestorePlan() : linuxRestorePlan();
    const ProtocolLine version = overTheWire(encodeVersionLine(number, text, plan));
    FUZZ_REQUIRE(version.kind == ProtocolLineKind::Version && version.version == number);
    FUZZ_REQUIRE(version.totalSteps == plan.size());
    FUZZ_REQUIRE(version.firstDestructiveStep == firstDestructiveStepOf(plan));
    const bool appSent = !expected.isEmpty() && !expected.contains(QLatin1Char(' '));
    FUZZ_REQUIRE(version.appVersion == (appSent ? expected : QString()));
    FUZZ_REQUIRE(acceptHelperAnnouncement(version, text, plan) == (number == RestoreProtocolVersion));
}

QString knownOrEmpty(const QString &value)
{
    return value.trimmed().isEmpty() ? QString() : value;
}

void fuzzRestoreArguments(Input &input)
{
    // NUL separates arguments, as it does in the argv the helper is given.
    QStringList arguments;
    for (const QByteArray &argument : input.rest().split('\0')) {
        arguments.append(QString::fromUtf8(argument));
    }

    for (const QString &argument : arguments) {
        if (isPlausibleDeviceNodePath(argument)) {
            FUZZ_REQUIRE(argument.startsWith(QStringLiteral("/dev/")));
            FUZZ_REQUIRE(argument.lastIndexOf(QLatin1Char('/')) == 4);
            FUZZ_REQUIRE(!argument.contains(QStringLiteral("..")));
        }
        if (isValidVolumeLabel(argument)) {
            FUZZ_REQUIRE(!argument.isEmpty() && argument.size() <= 11);
            FUZZ_REQUIRE(!argument.startsWith(QLatin1Char('-')));
            FUZZ_REQUIRE(argument.toLatin1() == argument.toUtf8());
        }
    }

    RestoreArguments parsed;
    QString error;
    if (!parseRestoreArguments(arguments, &parsed, &error)) {
        FUZZ_REQUIRE(!error.isEmpty());
        return;
    }

    const DiskInfo &claimed = parsed.expected;
    FUZZ_REQUIRE(isPlausibleDeviceNodePath(claimed.deviceId) && claimed.displayId == claimed.deviceId);
    FUZZ_REQUIRE(isValidVolumeLabel(parsed.volumeLabel));
    FUZZ_REQUIRE(claimed.size != 0 && claimed.sectorSize != 0);

    // The reason the helper may take arguments at all: none of them can say USB.
    FUZZ_REQUIRE(claimed.busType == 0);
    FUZZ_REQUIRE(!isSafeRestoreTarget(claimed, RestoreGuard{}));

    RestoreArguments again;
    FUZZ_REQUIRE(parseRestoreArguments(
        buildRestoreArguments(claimed, parsed.style, parsed.fileSystem, parsed.volumeLabel), &again));
    FUZZ_REQUIRE(again.expected.deviceId == claimed.deviceId);
    FUZZ_REQUIRE(again.expected.deviceSequence == claimed.deviceSequence);
    FUZZ_REQUIRE(again.expected.size == claimed.size && again.expected.sectorSize == claimed.sectorSize);
    FUZZ_REQUIRE(again.style == parsed.style && again.fileSystem == parsed.fileSystem);
    FUZZ_REQUIRE(again.volumeLabel == parsed.volumeLabel);
    FUZZ_REQUIRE(again.expected.serialNumber == knownOrEmpty(claimed.serialNumber));
    FUZZ_REQUIRE(again.expected.uniqueId == knownOrEmpty(claimed.uniqueId));
    FUZZ_REQUIRE(again.expected.path == knownOrEmpty(claimed.path));
    FUZZ_REQUIRE(again.expected.name == knownOrEmpty(claimed.name));
}

quint32 readLe32(const QByteArray &data, qsizetype offset)
{
    quint32 value = 0;
    for (int i = 0; i < 4; ++i) {
        value |= static_cast<quint32>(static_cast<quint8>(data.at(offset + i))) << (8 * i);
    }
    return value;
}

void checkGptHeader(const QByteArray &header)
{
    FUZZ_REQUIRE(header.startsWith(QByteArrayLiteral("EFI PART")));
    QByteArray zeroed = header.left(92);
    const quint32 stored = readLe32(zeroed, 16);
    zeroed.replace(16, 4, QByteArray(4, '\0'));
    FUZZ_REQUIRE(gptCrc32(zeroed) == stored);
}

void fuzzPartitionTable(Input &input)
{
    static constexpr quint32 commonSectorSizes[] = {512, 1024, 2048, 4096};

    PartitionTableRequest request;
    request.diskSize = input.take<std::uint64_t>();
    const quint32 sectorChoice = input.take<quint32>();
    // Mostly real sector sizes, so the builders are reached rather than refused.
    request.sectorSize = (sectorChoice & 0x80000000u) ? sectorChoice : commonSectorSizes[sectorChoice % 4];
    const std::uint8_t flags = input.take<std::uint8_t>();
    request.style = (flags & 1) ? PartitionStyle::Mbr : PartitionStyle::Gpt;
    static constexpr FileSystemType fileSystems[] = {
        FileSystemType::ExFat, FileSystemType::Fat32, FileSystemType::Ntfs, FileSystemType::Ext4};
    request.fileSystem = fileSystems[(flags >> 1) % 4];
    request.diskGuid = QByteArray(16, '\x11');
    request.partitionGuid = QByteArray(16, '\x22');
    request.partitionName = QString::fromUtf8(input.rest());

    request.layout = calculateGptLayout(request.diskSize, request.sectorSize);
    if (request.layout.length != 0) {
        FUZZ_REQUIRE(request.layout.startOffset + request.layout.length <= request.diskSize);
    }

    QString reason;
    if (!isWritablePartitionRequest(request, &reason)) {
        FUZZ_REQUIRE(!reason.isEmpty());
        return;
    }

    const quint64 sector = request.sectorSize;
    const quint64 diskEnd = (request.diskSize / sector) * sector;

    if (request.style == PartitionStyle::Mbr) {
        const QByteArray mbr = buildMbr(request);
        FUZZ_REQUIRE(static_cast<quint64>(mbr.size()) == sector);
        FUZZ_REQUIRE(static_cast<quint8>(mbr.at(510)) == 0x55 && static_cast<quint8>(mbr.at(511)) == 0xAA);
        return;
    }

    const QByteArray primary = buildGptPrimary(request);
    const QByteArray backup = buildGptBackup(request);
    FUZZ_REQUIRE(primary.size() % sector == 0 && backup.size() % sector == 0);
    FUZZ_REQUIRE(static_cast<quint64>(primary.size()) <= request.layout.startOffset);

    // The backup ends on the last whole sector and starts after the partition.
    const std::uint64_t backupOffset = gptBackupOffset(request);
    FUZZ_REQUIRE(backupOffset + static_cast<quint64>(backup.size()) == diskEnd);
    FUZZ_REQUIRE(request.layout.startOffset + request.layout.length <= backupOffset);

    checkGptHeader(primary.mid(static_cast<qsizetype>(sector), static_cast<qsizetype>(sector)));
    checkGptHeader(backup.right(static_cast<qsizetype>(sector)));
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    if (size == 0) {
        return 0;
    }
    Input input(data + 1, size - 1);

    // Letters keep the seed files readable; any other byte picks a target too.
    switch (data[0]) {
    case 'L':
        fuzzProtocolLines(input);
        return 0;
    case 'A':
        fuzzRestoreArguments(input);
        return 0;
    case 'T':
        fuzzPartitionTable(input);
        return 0;
    }
    switch (data[0] % 3) {
    case 0:
        fuzzProtocolLines(input);
        break;
    case 1:
        fuzzRestoreArguments(input);
        break;
    default:
        fuzzPartitionTable(input);
        break;
    }
    return 0;
}

#ifndef USBRESTORE_LIBFUZZER
int main(int argc, char **argv)
{
    for (int index = 1; index < argc; ++index) {
        std::ifstream file(argv[index], std::ios::binary);
        if (!file) {
            std::fprintf(stderr, "%s: cannot read\n", argv[index]);
            return 1;
        }
        const std::vector<char> bytes((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
        LLVMFuzzerTestOneInput(reinterpret_cast<const std::uint8_t *>(bytes.data()), bytes.size());
    }
    std::printf("Replayed %d inputs.\n", argc - 1);
    return 0;
}
#endif

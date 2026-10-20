#include "core/restore_protocol.h"

#include <QtGlobal>

#include <cstdint>
#include <limits>

namespace usbrestore {

namespace {

// Bounds what a helper spraying output can make the reader hold in memory.
constexpr int MaxProtocolTextLength = 2000;

constexpr int MaxDeviceNameLength = 64;

// The exFAT limit; mkfs would silently truncate a longer label.
constexpr int MaxVolumeLabelLength = 11;

constexpr char DeviceNodePrefix[] = "/dev/";

bool isDeviceNameCharacter(QChar character)
{
    if (character > QChar(0x7F)) {
        return false;
    }
    return character.isLetterOrNumber() || character == QLatin1Char('_') || character == QLatin1Char('-');
}

// Narrower than exFAT allows: less to reason about across a privilege boundary.
bool isVolumeLabelCharacter(QChar character)
{
    if (character > QChar(0x7F)) {
        return false;
    }
    return character.isLetterOrNumber() || character == QLatin1Char(' ') || character == QLatin1Char('_') ||
           character == QLatin1Char('-');
}

// Only the plain digits buildRestoreArguments() writes; QString::toULongLong()
// also takes a sign and surrounding whitespace.
bool parseDecimal(const QString &text, std::uint64_t maximum, std::uint64_t *value)
{
    if (text.isEmpty()) {
        return false;
    }
    std::uint64_t result = 0;
    for (const QChar character : text) {
        const char16_t code = character.unicode();
        if (code < u'0' || code > u'9') {
            return false;
        }
        const std::uint64_t digit = code - u'0';
        if (result > (maximum - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    *value = result;
    return true;
}

bool fail(QString *error, const QString &text)
{
    if (error) {
        *error = text;
    }
    return false;
}

} // namespace

bool isPlausibleDeviceNodePath(const QString &path)
{
    if (!path.startsWith(QLatin1String(DeviceNodePrefix))) {
        return false;
    }

    const QString name = path.mid(static_cast<int>(sizeof(DeviceNodePrefix)) - 1);
    if (name.isEmpty() || name.size() > MaxDeviceNameLength) {
        return false;
    }

    for (const QChar character : name) {
        if (!isDeviceNameCharacter(character)) {
            return false;
        }
    }
    return true;
}

bool isValidVolumeLabel(const QString &label)
{
    if (label.isEmpty() || label.size() > MaxVolumeLabelLength) {
        return false;
    }

    // No shell is involved, but mkfs would still parse a leading "-" as an option.
    if (label.startsWith(QLatin1Char('-'))) {
        return false;
    }
    if (label.trimmed() != label) {
        return false;
    }

    for (const QChar character : label) {
        if (!isVolumeLabelCharacter(character)) {
            return false;
        }
    }
    return true;
}

QStringList buildRestoreArguments(const DiskInfo &disk, PartitionStyle style, FileSystemType fileSystem,
                                  const QString &volumeLabel)
{
    QStringList arguments;
    arguments << QStringLiteral("--device") << disk.deviceId;
    arguments << QStringLiteral("--style")
              << (style == PartitionStyle::Mbr ? QStringLiteral("mbr") : QStringLiteral("gpt"));
    arguments << QStringLiteral("--filesystem") << fileSystemTypeToken(fileSystem);
    arguments << QStringLiteral("--label") << volumeLabel;
    arguments << QStringLiteral("--expect-size") << QString::number(disk.size);
    arguments << QStringLiteral("--expect-sector-size") << QString::number(disk.sectorSize);

    // isSameRestoreTarget() skips an identifier only one side reports, so omitting
    // an unknown value costs no more than not knowing it.
    const auto appendIfKnown = [&arguments](const QString &flag, const QString &value) {
        if (!value.trimmed().isEmpty()) {
            arguments << flag << value;
        }
    };
    appendIfKnown(QStringLiteral("--expect-serial"), disk.serialNumber);
    appendIfKnown(QStringLiteral("--expect-unique-id"), disk.uniqueId);
    appendIfKnown(QStringLiteral("--expect-path"), disk.path);
    appendIfKnown(QStringLiteral("--expect-name"), disk.name);

    return arguments;
}

bool parseRestoreArguments(const QStringList &arguments, RestoreArguments *parsed, QString *error)
{
    RestoreArguments result;
    QString device;
    QString style;
    QString fileSystem;
    QString size;
    QString sectorSize;
    QStringList seen;

    for (int index = 0; index < arguments.size(); index += 2) {
        const QString flag = arguments.at(index);
        if (index + 1 >= arguments.size()) {
            return fail(error, QStringLiteral("%1 was given without a value.").arg(flag));
        }
        const QString value = arguments.at(index + 1);
        if (seen.contains(flag)) {
            return fail(error, QStringLiteral("%1 was given more than once.").arg(flag));
        }
        seen.append(flag);

        if (flag == QStringLiteral("--device")) {
            device = value;
        } else if (flag == QStringLiteral("--style")) {
            style = value;
        } else if (flag == QStringLiteral("--filesystem")) {
            fileSystem = value;
        } else if (flag == QStringLiteral("--label")) {
            result.volumeLabel = value;
        } else if (flag == QStringLiteral("--expect-size")) {
            size = value;
        } else if (flag == QStringLiteral("--expect-sector-size")) {
            sectorSize = value;
        } else if (flag == QStringLiteral("--expect-serial")) {
            result.expected.serialNumber = value;
        } else if (flag == QStringLiteral("--expect-unique-id")) {
            result.expected.uniqueId = value;
        } else if (flag == QStringLiteral("--expect-path")) {
            result.expected.path = value;
        } else if (flag == QStringLiteral("--expect-name")) {
            result.expected.name = value;
        } else {
            return fail(error, QStringLiteral("%1 is not a recognised option.").arg(flag));
        }
    }

    if (!isPlausibleDeviceNodePath(device)) {
        return fail(error, QStringLiteral("--device must name a block device under /dev."));
    }
    if (style == QStringLiteral("gpt")) {
        result.style = PartitionStyle::Gpt;
    } else if (style == QStringLiteral("mbr")) {
        result.style = PartitionStyle::Mbr;
    } else {
        return fail(error, QStringLiteral("--style must be \"gpt\" or \"mbr\"."));
    }
    if (!parseFileSystemType(fileSystem, &result.fileSystem)) {
        return fail(error, QStringLiteral("--filesystem must be \"exfat\", \"fat32\", \"ntfs\" or \"ext4\"."));
    }
    if (!isValidVolumeLabel(result.volumeLabel)) {
        return fail(error, QStringLiteral("--label must be 1 to %1 letters, digits, spaces, \"-\" or \"_\".")
                               .arg(MaxVolumeLabelLength));
    }

    std::uint64_t number = 0;
    if (!parseDecimal(size, std::numeric_limits<std::uint64_t>::max(), &number) || number == 0) {
        return fail(error, QStringLiteral("--expect-size must be the disk size in bytes."));
    }
    result.expected.size = number;
    if (!parseDecimal(sectorSize, std::numeric_limits<quint32>::max(), &number) || number == 0) {
        return fail(error, QStringLiteral("--expect-sector-size must be the logical sector size in bytes."));
    }
    result.expected.sectorSize = static_cast<quint32>(number);

    result.expected.deviceId = device;
    result.expected.displayId = device;
    // busType stays 0: the caller cannot assert USB, and a DiskInfo built from argv
    // alone must fail isSafeRestoreTarget().

    if (parsed) {
        *parsed = result;
    }
    return true;
}

QString describeRestoreExit(int exitCode, const QString &helperStderr)
{
    const QString reported = sanitizeProtocolText(helperStderr);

    switch (exitCode) {
    case RestoreExitSuccess:
    case RestoreExitCancelled:
        return {};

    case RestoreExitFailed:
        // Fallback for a helper that died before writing to stderr.
        return reported.isEmpty() ? QStringLiteral("The restore failed.") : reported;

    case RestoreExitUsage:
        return QStringLiteral("USB Restoration Tool and its restore helper do not agree on how to talk to each "
                              "other, which usually means a partly upgraded installation. Reinstall the "
                              "application.%1")
            .arg(reported.isEmpty() ? QString() : QStringLiteral("\n\nThe helper said: %1").arg(reported));

    // pkexec's own, not the helper's.
    case 126:
        return QStringLiteral("Authentication was cancelled, so nothing on the drive was changed.");
    case 127:
        return QStringLiteral("The restore helper could not be started. Check that USB Restoration Tool is "
                              "installed rather than run from a build directory.");

    default:
        return reported.isEmpty()
                   ? QStringLiteral("The restore helper stopped unexpectedly (exit code %1).").arg(exitCode)
                   : QStringLiteral("The restore helper stopped unexpectedly (exit code %1): %2")
                         .arg(exitCode)
                         .arg(reported);
    }
}

HelperRunVerdict judgeHelperRun(const HelperRunObservation &run, QString *error)
{
    const auto failWith = [error](const QString &text) {
        if (error) {
            *error = text;
        }
        return HelperRunVerdict::Failed;
    };

    // Died, or pkexec reported auth dismissed, before any version line: the helper
    // never ran. The only case a requested kill is believed.
    if (run.killRequested && !run.sawVersion && (run.crashed || run.exitCode == 126)) {
        return HelperRunVerdict::Cancelled;
    }

    if (run.crashed) {
        return failWith(QStringLiteral("The restore helper stopped unexpectedly. Check the drive with a partition "
                                       "tool before using it."));
    }

    // Before the exit code: no line from a mismatched helper can be trusted, its
    // "success" or "cancelled" included.
    if (run.sawVersion && !run.versionAccepted) {
        return failWith(run.versionError.isEmpty() ? describeRestoreExit(RestoreExitUsage, run.helperStderr)
                                                   : run.versionError);
    }

    if (!run.sawVersion) {
        // The exit code is pkexec's or a stranger's: pass 126/127 through, but do
        // not believe a success or cancellation from a process that never spoke.
        const bool claimsFinished =
            run.exitCode == RestoreExitSuccess || run.exitCode == RestoreExitCancelled;
        return failWith(describeRestoreExit(claimsFinished ? RestoreExitUsage : run.exitCode, run.helperStderr));
    }

    if (run.exitCode == RestoreExitCancelled) {
        return HelperRunVerdict::Cancelled;
    }
    if (run.exitCode != RestoreExitSuccess) {
        return failWith(describeRestoreExit(run.exitCode, run.helperStderr));
    }

    if (run.location.isEmpty()) {
        return failWith(QStringLiteral("The restore helper finished without reporting where the volume ended up. "
                                       "Check the drive with a partition tool before using it."));
    }

    return HelperRunVerdict::Succeeded;
}

QString sanitizeProtocolText(const QString &text)
{
    QString sanitized;
    sanitized.reserve(qMin(text.size(), MaxProtocolTextLength));

    bool pendingSpace = false;
    for (const QChar character : text) {
        const bool isSeparator = character.isSpace() || character.category() == QChar::Other_Control;
        if (isSeparator) {
            pendingSpace = !sanitized.isEmpty();
            continue;
        }
        // The pending space counts too, or it can carry the text one past the limit.
        if (sanitized.size() + (pendingSpace ? 2 : 1) > MaxProtocolTextLength) {
            break;
        }
        if (pendingSpace) {
            sanitized.append(QLatin1Char(' '));
            pendingSpace = false;
        }
        sanitized.append(character);
    }
    // A lone half of a surrogate pair would not survive the UTF-8 trip to the GUI,
    // and dropping it can expose the space before it.
    if (!sanitized.isEmpty() && sanitized.back().isHighSurrogate()) {
        sanitized.chop(1);
    }
    if (sanitized.endsWith(QLatin1Char(' '))) {
        sanitized.chop(1);
    }
    return sanitized;
}

QString encodeVersionLine(int version, const QString &appVersion, const QVector<RestoreStep> &plan)
{
    QString line = QStringLiteral("version %1").arg(version);
    const QString app = sanitizeProtocolText(appVersion);
    if (!app.isEmpty() && !app.contains(QLatin1Char(' '))) {
        line += QStringLiteral(" app %1").arg(app);
    }
    return line + QStringLiteral(" steps %1 destructive %2").arg(plan.size()).arg(firstDestructiveStepOf(plan));
}

QString encodeStepLine(int step, int totalSteps, const QString &message)
{
    return QStringLiteral("step %1/%2 %3").arg(step).arg(totalSteps).arg(sanitizeProtocolText(message));
}

QString encodeDetailLine(const QString &message)
{
    return QStringLiteral("detail %1").arg(sanitizeProtocolText(message));
}

QString encodeResultLine(const QString &location)
{
    return QStringLiteral("result %1").arg(sanitizeProtocolText(location));
}

ProtocolLine parseProtocolLine(const QString &line)
{
    ProtocolLine parsed;
    const QString trimmed = line.trimmed();

    if (trimmed.isEmpty()) {
        return parsed;
    }
    // The reader trims, so an encoder's empty message arrives as a bare keyword.
    const int firstSpace = trimmed.indexOf(QLatin1Char(' '));
    const QString keyword = firstSpace < 0 ? trimmed : trimmed.left(firstSpace);
    const QString remainder = firstSpace < 0 ? QString() : trimmed.mid(firstSpace + 1);

    bool ok = false;
    if (keyword == QStringLiteral("version")) {
        const QStringList fields = remainder.split(QLatin1Char(' '), Qt::SkipEmptyParts);
        const int version = fields.value(0).toInt(&ok);
        if (!ok) {
            return parsed;
        }
        parsed.kind = ProtocolLineKind::Version;
        parsed.version = version;

        // Malformed details leave all of them unset, which acceptHelperAnnouncement() refuses.
        ProtocolLine details = parsed;
        QStringList seen;
        for (int index = 1; index < fields.size(); index += 2) {
            const QString key = fields.at(index);
            const QString value = fields.value(index + 1);
            if (value.isEmpty() || seen.contains(key)) {
                return parsed;
            }
            seen.append(key);
            if (key == QStringLiteral("app")) {
                details.appVersion = value;
            } else if (key == QStringLiteral("steps")) {
                details.totalSteps = value.toInt(&ok);
            } else if (key == QStringLiteral("destructive")) {
                details.firstDestructiveStep = value.toInt(&ok);
            } else {
                ok = false;
            }
            if (!ok) {
                return parsed;
            }
        }
        return details;
    }

    if (keyword == QStringLiteral("step")) {
        const int countEnd = remainder.indexOf(QLatin1Char(' '));
        if (countEnd == 0 || remainder.isEmpty()) {
            return parsed;
        }
        const QStringList counts = (countEnd < 0 ? remainder : remainder.left(countEnd)).split(QLatin1Char('/'));
        if (counts.size() != 2) {
            return parsed;
        }
        const int step = counts.at(0).toInt(&ok);
        if (!ok) {
            return parsed;
        }
        const int totalSteps = counts.at(1).toInt(&ok);
        if (!ok) {
            return parsed;
        }
        parsed.kind = ProtocolLineKind::Step;
        parsed.step = step;
        parsed.totalSteps = totalSteps;
        parsed.text = countEnd < 0 ? QString() : remainder.mid(countEnd + 1);
        return parsed;
    }

    if (keyword == QStringLiteral("detail")) {
        parsed.kind = ProtocolLineKind::Detail;
        parsed.text = remainder;
        return parsed;
    }

    if (keyword == QStringLiteral("result")) {
        parsed.kind = ProtocolLineKind::Result;
        parsed.text = remainder;
        return parsed;
    }

    return parsed;
}

bool acceptHelperAnnouncement(const ProtocolLine &announcement, const QString &ourAppVersion,
                              const QVector<RestoreStep> &plan, QString *error)
{
    if (announcement.kind != ProtocolLineKind::Version) {
        return fail(error, describeRestoreExit(RestoreExitUsage, QString()));
    }

    const bool samePlan = announcement.totalSteps == plan.size() &&
                          announcement.firstDestructiveStep == firstDestructiveStepOf(plan);
    if (announcement.version == RestoreProtocolVersion && samePlan) {
        return true;
    }

    QString theirs;
    if (announcement.appVersion.isEmpty()) {
        theirs = QStringLiteral("an older version");
    } else if (announcement.appVersion == ourAppVersion) {
        theirs = QStringLiteral("a different build of version %1").arg(announcement.appVersion);
    } else {
        theirs = QStringLiteral("version %1").arg(announcement.appVersion);
    }
    return fail(error, QStringLiteral("USB Restoration Tool is version %1, but its restore helper is from %2, so "
                                      "the two disagree about how a restore proceeds. This usually means a partly "
                                      "upgraded installation. Reinstall the application so both come from the same "
                                      "release.")
                           .arg(ourAppVersion, theirs));
}

} // namespace usbrestore

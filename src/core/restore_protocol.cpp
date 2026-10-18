#include "core/restore_protocol.h"

#include <QtGlobal>

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

    bool ok = false;
    result.expected.size = size.toULongLong(&ok);
    if (!ok || result.expected.size == 0) {
        return fail(error, QStringLiteral("--expect-size must be the disk size in bytes."));
    }
    result.expected.sectorSize = sectorSize.toUInt(&ok);
    if (!ok || result.expected.sectorSize == 0) {
        return fail(error, QStringLiteral("--expect-sector-size must be the logical sector size in bytes."));
    }

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
        return failWith(describeRestoreExit(RestoreExitUsage, run.helperStderr));
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
        if (pendingSpace) {
            sanitized.append(QLatin1Char(' '));
            pendingSpace = false;
        }
        sanitized.append(character);
        if (sanitized.size() >= MaxProtocolTextLength) {
            break;
        }
    }
    return sanitized;
}

QString encodeVersionLine(int version)
{
    return QStringLiteral("version %1").arg(version);
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

    const int firstSpace = trimmed.indexOf(QLatin1Char(' '));
    if (firstSpace <= 0) {
        return parsed;
    }
    const QString keyword = trimmed.left(firstSpace);
    const QString remainder = trimmed.mid(firstSpace + 1);

    bool ok = false;
    if (keyword == QStringLiteral("version")) {
        const int version = remainder.toInt(&ok);
        if (!ok) {
            return parsed;
        }
        parsed.kind = ProtocolLineKind::Version;
        parsed.version = version;
        return parsed;
    }

    if (keyword == QStringLiteral("step")) {
        const int countEnd = remainder.indexOf(QLatin1Char(' '));
        if (countEnd <= 0) {
            return parsed;
        }
        const QStringList counts = remainder.left(countEnd).split(QLatin1Char('/'));
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
        parsed.text = remainder.mid(countEnd + 1);
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

} // namespace usbrestore

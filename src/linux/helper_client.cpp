#include "linux/helper_client.h"

#include "core/restore_protocol.h"

#include <QFileInfo>
#include <QProcess>

#ifndef USBRESTORE_HELPER_PATH
#define USBRESTORE_HELPER_PATH "/usr/libexec/usb-restoration-helper"
#endif

namespace usbrestore {

namespace {

// How long to wait for pkexec itself to appear. The authentication dialog that
// follows is not on a clock: the user may take as long as they like, and a
// timeout here would cancel a restore they were in the middle of authorising.
constexpr int ProcessStartTimeoutMs = 30 * 1000;

// Long enough for a fork and a QString; this invocation does no work.
constexpr int VersionProbeTimeoutMs = 10 * 1000;

// How often the output loop wakes to look at cancellation. Short enough that
// Cancel feels immediate, long enough not to spin.
constexpr int PollIntervalMs = 200;

// A helper that emits this much without a newline is malfunctioning, and the
// reader must not grow to match it.
constexpr int MaxBufferedBytes = 64 * 1024;

// pkexec is found at a fixed path for the same reason mkfs is: $PATH is exactly
// what an attacker who can influence this process's environment would arrange.
// Being wrong here would mean prompting the user to authorise the wrong binary.
QString findPkexec()
{
    static const QStringList candidates = {
        QStringLiteral("/usr/bin/pkexec"),
        QStringLiteral("/bin/pkexec"),
        QStringLiteral("/usr/local/bin/pkexec"),
    };
    for (const QString &candidate : candidates) {
        const QFileInfo info(candidate);
        if (info.exists() && info.isExecutable() && !info.isDir()) {
            return candidate;
        }
    }
    return {};
}

// Asked before pkexec, not after. The probe needs no privilege — the helper
// prints its version and exits before it checks anything — so a mismatched pair
// is caught without making the user type a password first.
bool helperSpeaksOurProtocol(const QString &helper, QString *error)
{
    QProcess process;
    process.setProgram(helper);
    process.setArguments({QString::fromLatin1(ProtocolVersionFlag)});
    process.start();
    if (!process.waitForStarted(VersionProbeTimeoutMs) || !process.waitForFinished(VersionProbeTimeoutMs)) {
        process.kill();
        process.waitForFinished(1000);
        if (error) {
            *error = QStringLiteral("The restore helper at %1 could not be started.").arg(helper);
        }
        return false;
    }

    const ProtocolLine line = parseProtocolLine(QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed());
    if (line.kind != ProtocolLineKind::Version || line.version != RestoreProtocolVersion) {
        if (error) {
            *error = describeRestoreExit(RestoreExitUsage, QString());
        }
        return false;
    }
    return true;
}

// Splits whatever has arrived so far into whole lines, keeping any partial last
// line in the buffer for the next read.
QStringList takeCompleteLines(QByteArray &buffer)
{
    QStringList lines;
    int newline = buffer.indexOf('\n');
    while (newline >= 0) {
        lines.append(QString::fromLocal8Bit(buffer.left(newline)).trimmed());
        buffer.remove(0, newline + 1);
        newline = buffer.indexOf('\n');
    }

    if (buffer.size() > MaxBufferedBytes) {
        lines.append(QString::fromLocal8Bit(buffer));
        buffer.clear();
    }
    return lines;
}

} // namespace

QString helperExecutablePath()
{
    return QStringLiteral(USBRESTORE_HELPER_PATH);
}

bool isHelperAvailable()
{
    const QFileInfo helper(helperExecutablePath());
    return helper.exists() && helper.isExecutable() && !helper.isDir() && !findPkexec().isEmpty();
}

bool runHelperRestore(const RestoreRequest &request,
                      RestoreReporter &reporter,
                      RestoreResult *result,
                      QString *error)
{
    const QString helper = helperExecutablePath();
    const QFileInfo helperInfo(helper);
    if (!helperInfo.exists() || !helperInfo.isExecutable() || helperInfo.isDir()) {
        if (error) {
            *error = QStringLiteral("The restore helper was not found at %1. USB Restoration Tool has to be "
                                    "installed to restore a drive without running the whole application as root.")
                         .arg(helper);
        }
        return false;
    }

    const QString pkexec = findPkexec();
    if (pkexec.isEmpty()) {
        if (error) {
            *error = QStringLiteral("pkexec was not found, so permission to restore the drive cannot be "
                                    "requested. Install polkit, or run USB Restoration Tool with sudo.");
        }
        return false;
    }

    if (!helperSpeaksOurProtocol(helper, error)) {
        return false;
    }

    QProcess process;
    process.setProgram(pkexec);
    process.setArguments(QStringList{helper} +
                         buildRestoreArguments(request.disk, request.style, request.volumeLabel));
    // Separate channels: stdout carries the protocol and stderr carries the one
    // error line, and merging them would let a message from mkfs be read as a
    // protocol line.
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(ProcessStartTimeoutMs)) {
        if (error) {
            *error = QStringLiteral("pkexec could not be started, so permission to restore the drive could not be "
                                    "requested.");
        }
        return false;
    }

    QByteArray buffer;
    QString location;
    bool sawVersion = false;
    bool versionAccepted = false;
    bool cancelSent = false;
    bool killedWhileAuthenticating = false;

    const auto consume = [&](const QStringList &lines) {
        for (const QString &line : lines) {
            if (line.isEmpty()) {
                continue;
            }
            const ProtocolLine parsed = parseProtocolLine(line);
            switch (parsed.kind) {
            case ProtocolLineKind::Version:
                sawVersion = true;
                versionAccepted = parsed.version == RestoreProtocolVersion;
                break;
            case ProtocolLineKind::Step:
                reporter.step(parsed.text);
                break;
            case ProtocolLineKind::Detail:
                reporter.detail(parsed.text);
                break;
            case ProtocolLineKind::Result:
                location = parsed.text;
                break;
            case ProtocolLineKind::Unknown:
                // Kept rather than dropped: it is the helper's own output, and
                // the log is where an unexplained line is worth having.
                reporter.detail(QStringLiteral("helper: %1").arg(line));
                break;
            }
        }
    };

    while (process.state() != QProcess::NotRunning) {
        process.waitForReadyRead(PollIntervalMs);
        buffer.append(process.readAllStandardOutput());
        consume(takeCompleteLines(buffer));

        if (reporter.cancelRequested() && !cancelSent) {
            cancelSent = true;
            if (!sawVersion) {
                // Still at the authentication dialog: the helper has not run,
                // so there is nothing to ask politely and nothing to damage.
                killedWhileAuthenticating = true;
                process.kill();
            } else {
                // Running. Ask, and let it decide — it stops only where
                // stopping is still harmless, exactly as an in-process restore
                // does.
                process.write(CancelRequestToken);
                process.write("\n");
            }
        }
    }

    process.waitForFinished(-1);
    buffer.append(process.readAllStandardOutput());
    consume(takeCompleteLines(buffer));
    if (!buffer.isEmpty()) {
        consume({QString::fromLocal8Bit(buffer).trimmed()});
    }

    const QString helperStderr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();

    if (killedWhileAuthenticating) {
        // Cancelled before anything could happen. RestoreWorker reads "false
        // with no error" as a cancellation.
        return false;
    }

    if (process.exitStatus() != QProcess::NormalExit) {
        if (error) {
            *error = QStringLiteral("The restore helper stopped unexpectedly. Check the drive with a partition "
                                    "tool before using it.");
        }
        return false;
    }

    const int exitCode = process.exitCode();
    if (exitCode == RestoreExitCancelled) {
        return false;
    }
    if (exitCode != RestoreExitSuccess) {
        if (error) {
            *error = describeRestoreExit(exitCode, helperStderr);
        }
        return false;
    }

    // A helper that never announced a version it agrees on, or that reports
    // success without saying where the volume ended up, did not do what this
    // code thinks it did. Neither should be possible; both are refused rather
    // than reported as a restore that worked.
    if (!sawVersion || !versionAccepted) {
        if (error) {
            *error = describeRestoreExit(RestoreExitUsage, helperStderr);
        }
        return false;
    }
    if (location.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The restore helper finished without reporting where the volume ended up. "
                                    "Check the drive with a partition tool before using it.");
        }
        return false;
    }

    if (result) {
        result->location = location;
    }
    return true;
}

} // namespace usbrestore

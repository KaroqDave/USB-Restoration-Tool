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
                         buildRestoreArguments(request.disk, request.style, request.fileSystem, request.volumeLabel));
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
    bool cancelTokenWritten = false;
    bool killRequested = false;

    const auto askHelperToStop = [&]() {
        if (cancelTokenWritten) {
            return;
        }
        cancelTokenWritten = true;
        process.write(CancelRequestToken);
        process.write("\n");
    };

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
                // The helper announces its version before touching the disk
                // precisely so that a caller that does not recognise it can
                // stop the run — so stop it here, not in the verdict after it
                // has finished. The process is root, so a signal from here
                // would fail; the cancel token is the one lever, and any
                // helper honours it at its first checkpoint, before anything
                // is written. helperSpeaksOurProtocol() makes this reachable
                // only when the binary changed between the probe and the run.
                if (!versionAccepted) {
                    askHelperToStop();
                }
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

        if (reporter.cancelRequested() && !cancelTokenWritten) {
            if (sawVersion) {
                // Running. Ask, and let it decide — it stops only where
                // stopping is still harmless, exactly as an in-process restore
                // does.
                askHelperToStop();
            } else if (!killRequested) {
                // No version line yet, so as far as anyone can tell this is
                // still pkexec at the authentication dialog: nothing to ask
                // politely and nothing to damage. But the kill is a request,
                // not a fact — once pkexec authenticates and execs the helper,
                // the process is root, this process may no longer signal it,
                // and the SIGKILL fails without a word. If a version line
                // arrives anyway, that is what happened, and the branch above
                // writes the cancel token on that turn instead.
                killRequested = true;
                process.kill();
            }
        }
    }

    process.waitForFinished(-1);
    buffer.append(process.readAllStandardOutput());
    consume(takeCompleteLines(buffer));
    if (!buffer.isEmpty()) {
        consume({QString::fromLocal8Bit(buffer).trimmed()});
    }

    // The verdict lives in core (judgeHelperRun) rather than here, because the
    // order of its checks is a safety rule: a requested kill is only believed
    // when the process died without ever speaking, and a rejected version
    // outranks whatever the exit code claims.
    HelperRunObservation run;
    run.crashed = process.exitStatus() != QProcess::NormalExit;
    run.exitCode = process.exitCode();
    run.sawVersion = sawVersion;
    run.versionAccepted = versionAccepted;
    run.killRequested = killRequested;
    run.location = location;
    run.helperStderr = QString::fromLocal8Bit(process.readAllStandardError()).trimmed();

    QString verdictError;
    switch (judgeHelperRun(run, &verdictError)) {
    case HelperRunVerdict::Cancelled:
        // RestoreWorker reads "false with no error" as a cancellation.
        return false;
    case HelperRunVerdict::Failed:
        if (error) {
            *error = verdictError;
        }
        return false;
    case HelperRunVerdict::Succeeded:
        break;
    }

    if (result) {
        result->location = location;
    }
    return true;
}

} // namespace usbrestore

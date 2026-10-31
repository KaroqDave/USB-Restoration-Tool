#include "linux/helper_client.h"

#include "core/restore_protocol.h"

#include <QFileInfo>
#include <QProcess>

#ifndef USBRESTORE_APP_VERSION
#error "USBRESTORE_APP_VERSION must be defined by the build"
#endif

#ifndef USBRESTORE_HELPER_PATH
#define USBRESTORE_HELPER_PATH "/usr/libexec/usb-restoration-helper"
#endif

namespace usbrestore {

namespace {

// Covers only pkexec starting; the authentication dialog after it is not timed.
constexpr int ProcessStartTimeoutMs = 30 * 1000;

constexpr int VersionProbeTimeoutMs = 10 * 1000;

constexpr int PollIntervalMs = 200;

// A helper that emits this much without a newline is malfunctioning.
constexpr int MaxBufferedBytes = 64 * 1024;

// Fixed paths, not $PATH, which a caller controlling the environment could redirect.
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

bool acceptAnnouncement(const ProtocolLine &line, QString *error)
{
    return acceptHelperAnnouncement(line, QStringLiteral(USBRESTORE_APP_VERSION), linuxRestorePlan(), error);
}

// Unprivileged, so a mismatched helper is caught before the password prompt.
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

    return acceptAnnouncement(
        parseProtocolLine(QString::fromLocal8Bit(process.readAllStandardOutput()).trimmed()), error);
}

// Leaves a partial last line in the buffer for the next read.
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

bool runHelperRestore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error)
{
    const QString helper = helperExecutablePath();
    const QFileInfo helperInfo(helper);
    if (!helperInfo.exists() || !helperInfo.isExecutable() || helperInfo.isDir()) {
        if (error) {
            *error = QStringLiteral(
                         "The restore helper was not found at %1. USB Restoration Tool has to be "
                         "installed to restore a drive without running the whole application as root.")
                         .arg(helper);
        }
        return false;
    }

    const QString pkexec = findPkexec();
    if (pkexec.isEmpty()) {
        if (error) {
            *error = QStringLiteral(
                "pkexec was not found, so permission to restore the drive cannot be "
                "requested. Install polkit, or run USB Restoration Tool with sudo.");
        }
        return false;
    }

    if (!helperSpeaksOurProtocol(helper, error)) {
        return false;
    }

    QProcess process;
    process.setProgram(pkexec);
    process.setArguments(
        QStringList{helper} +
        buildRestoreArguments(request.disk, request.style, request.fileSystem, request.volumeLabel));
    // Merged channels would let mkfs output be read as a protocol line.
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(ProcessStartTimeoutMs)) {
        if (error) {
            *error = QStringLiteral(
                "pkexec could not be started, so permission to restore the drive could not be "
                "requested.");
        }
        return false;
    }

    QByteArray buffer;
    QString location;
    bool sawVersion = false;
    bool versionAccepted = false;
    QString versionError;
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
                // A second announcement is a malfunction, and must not undo a refusal.
                versionAccepted = !sawVersion && acceptAnnouncement(parsed, &versionError);
                sawVersion = true;
                // Stop now, before the disk is touched; the helper is root, so the
                // cancel token is the only lever. Reachable only if the binary changed.
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
                askHelperToStop();
            } else if (!killRequested) {
                // Probably still the auth dialog. If pkexec already exec'd the root helper
                // the kill fails silently; a later version line then sends the token instead.
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

    HelperRunObservation run;
    run.crashed = process.exitStatus() != QProcess::NormalExit;
    run.exitCode = process.exitCode();
    run.sawVersion = sawVersion;
    run.versionAccepted = versionAccepted;
    run.versionError = versionError;
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

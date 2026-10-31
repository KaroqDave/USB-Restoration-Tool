// usb-restoration-helper: the root half of a Linux restore (docs/polkit-helper.md).
// Any local process can start it, so argv is only a set of claims to check.

#include "core/restore_protocol.h"
#include "core/safety.h"
#include "linux/linux_enumerate.h"
#include "linux/linux_restore.h"
#include "platform/disk_service.h"
#include "platform/startup.h"

#include <QCoreApplication>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <poll.h>
#include <signal.h>
#include <unistd.h>

#include <cstdio>

namespace {

void ignoreSignal(int) {}

// Lets the restore outlive the GUI. A no-op handler, not SIG_IGN, which mkfs
// would inherit across exec.
void outliveCaller()
{
    struct sigaction action = {};
    action.sa_handler = ignoreSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_RESTART;
    ::sigaction(SIGPIPE, &action, nullptr);
    ::sigaction(SIGHUP, &action, nullptr);
}

void writeLine(const QString &line)
{
    QTextStream out(stdout);
    out << line << '\n';
    out.flush();
}

void announceVersion()
{
    writeLine(
        usbrestore::encodeVersionLine(
            usbrestore::RestoreProtocolVersion,
            QStringLiteral(USBRESTORE_APP_VERSION),
            usbrestore::linuxRestorePlan()));
}

void writeError(const QString &message)
{
    QTextStream err(stderr);
    err << usbrestore::sanitizeProtocolText(message) << '\n';
    err.flush();
}

class HelperReporter : public usbrestore::RestoreReporter {
  public:
    void step(const QString &message) override
    {
        const int total = static_cast<int>(usbrestore::linuxRestorePlan().size());
        m_step = qMin(m_step + 1, total);
        writeLine(usbrestore::encodeStepLine(m_step, total, message));
    }

    void detail(const QString &message) override
    {
        // mkfs output arrives as several lines at once; keep them as separate lines.
        const QStringList lines = message.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const QString sanitized = usbrestore::sanitizeProtocolText(line);
            if (!sanitized.isEmpty()) {
                writeLine(usbrestore::encodeDetailLine(sanitized));
            }
        }
    }

    bool cancelRequested() const override
    {
        if (m_cancelled) {
            return true;
        }

        // Don't steal input from a terminal; a helper run by hand is cancelled with Ctrl-C.
        if (::isatty(STDIN_FILENO) == 1) {
            return false;
        }

        struct pollfd descriptor = {};
        descriptor.fd = STDIN_FILENO;
        descriptor.events = POLLIN;
        if (::poll(&descriptor, 1, 0) <= 0 || (descriptor.revents & POLLIN) == 0) {
            return false;
        }

        char buffer[64] = {};
        const ssize_t count = ::read(STDIN_FILENO, buffer, sizeof(buffer) - 1);
        if (count <= 0) {
            // The caller is gone, which is not a cancellation: finishing beats a
            // half-written partition table.
            return false;
        }

        m_request.append(buffer, static_cast<int>(count));
        m_cancelled = m_request.contains(usbrestore::CancelRequestToken);
        return m_cancelled;
    }

  private:
    int m_step = 0;
    mutable QByteArray m_request;
    mutable bool m_cancelled = false;
};

} // namespace

int main(int argc, char *argv[])
{
    usbrestore::hardenProcessStartup();
    outliveCaller();

    // QProcess needs an application object; exec() is deliberately never called.
    const QCoreApplication app(argc, argv);

    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }

    if (arguments.size() == 1 && arguments.first() == QLatin1String(usbrestore::ProtocolVersionFlag)) {
        announceVersion();
        return usbrestore::RestoreExitSuccess;
    }

    // Before any work, so a caller that rejects the version can still cancel.
    announceVersion();

    if (::geteuid() != 0) {
        writeError(QStringLiteral(
            "usb-restoration-helper must run as root. It is started by USB Restoration Tool "
            "through pkexec, not on its own."));
        return usbrestore::RestoreExitUsage;
    }

    usbrestore::RestoreArguments parsed;
    QString error;
    if (!usbrestore::parseRestoreArguments(arguments, &parsed, &error)) {
        writeError(error);
        return usbrestore::RestoreExitUsage;
    }

    // Claims from argv are only ever used to refuse.
    const usbrestore::RestoreGuard guard = usbrestore::linuxRestoreGuard();

    usbrestore::DiskInfo disk;
    if (!usbrestore::refreshUsbDisk(parsed.expected.deviceId, &disk, &error, usbrestore::DiskDetails::Restore)) {
        writeError(error);
        return usbrestore::RestoreExitFailed;
    }

    // The defence against a malicious caller: judged on fresh facts only.
    if (!usbrestore::isSafeRestoreTarget(disk, guard, &error)) {
        writeError(error);
        return usbrestore::RestoreExitFailed;
    }

    // Catches a stick swapped during the polkit prompt; not a defence against a dishonest caller.
    if (!usbrestore::isSameRestoreTarget(parsed.expected, disk, &error)) {
        writeError(error);
        return usbrestore::RestoreExitFailed;
    }

    usbrestore::RestoreRequest request;
    request.disk = disk;
    request.style = parsed.style;
    request.fileSystem = parsed.fileSystem;
    request.volumeLabel = parsed.volumeLabel;
    request.guard = guard;

    HelperReporter reporter;
    usbrestore::RestoreResult result;
    if (!usbrestore::performLinuxRestore(request, reporter, &result, &error)) {
        // A distinct exit code, so the GUI need not guess from an empty stderr.
        if (reporter.cancelRequested() && error.isEmpty()) {
            return usbrestore::RestoreExitCancelled;
        }
        writeError(
            error.isEmpty() ? QStringLiteral(
                                  "The restore stopped. Unplug the drive, plug it back in and try "
                                  "again.")
                            : error);
        return usbrestore::RestoreExitFailed;
    }

    writeLine(usbrestore::encodeResultLine(result.location));
    return usbrestore::RestoreExitSuccess;
}

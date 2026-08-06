// usb-restoration-helper — the privileged half of a Linux restore.
//
// Started by the unprivileged GUI through pkexec, performs one restore, and
// exits. It has no window, no settings and no event loop; see
// docs/polkit-helper.md for why it exists at all.
//
// The contract with the caller is that there is no contract. Every argument is
// a claim about a disk, and this process decides for itself, from sysfs, what
// is true and whether the restore may happen. Any local process can invoke this
// binary — that is what "the GUI is unprivileged" means — so the argument
// handling below is the security boundary, not a convenience.

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
#include <unistd.h>

#include <cstdio>

namespace {

void writeLine(const QString &line)
{
    QTextStream out(stdout);
    out << line << '\n';
    out.flush();
}

void writeError(const QString &message)
{
    QTextStream err(stderr);
    err << usbrestore::sanitizeProtocolText(message) << '\n';
    err.flush();
}

// Turns the backend's reporter calls into protocol lines, and answers the
// cancellation question from whatever the caller has written to stdin.
class HelperReporter : public usbrestore::RestoreReporter {
  public:
    void step(const QString &message) override
    {
        m_step = qMin(m_step + 1, usbrestore::LinuxRestoreStepCount);
        writeLine(usbrestore::encodeStepLine(m_step, usbrestore::LinuxRestoreStepCount, message));
    }

    void detail(const QString &message) override
    {
        // mkfs output arrives as several lines at once. Each becomes its own
        // detail line rather than one line with the newlines flattened out of
        // it, so the log on the far side reads the way it does today.
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

        // Only ever asked at the two points where a restore is still
        // reversible, so a slow or absent answer costs nothing.
        //
        // Reading the terminal's input would be stealing it from whoever is
        // sitting at it, so the cancel channel exists only when stdin is a pipe
        // — which is how the GUI starts this. A helper run by hand is cancelled
        // with Ctrl-C instead.
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
            // End of input means the caller is gone. That is not a reason to
            // stop: the restore either has not started, in which case it will
            // fail harmlessly on its own, or it has, in which case finishing is
            // better than leaving a half-written partition table behind.
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
    // Disables core dumps and tightens the file mode before anything else runs.
    // This process is root by definition, so it matters more here than in the
    // GUI it was split out of.
    usbrestore::hardenProcessStartup();

    // QProcess needs an application object to exist; nothing here needs it to
    // run, and exec() is deliberately never called.
    const QCoreApplication app(argc, argv);

    QStringList arguments = QCoreApplication::arguments();
    if (!arguments.isEmpty()) {
        arguments.removeFirst();
    }

    if (arguments.size() == 1 && arguments.first() == QLatin1String(usbrestore::ProtocolVersionFlag)) {
        writeLine(usbrestore::encodeVersionLine(usbrestore::RestoreProtocolVersion));
        return usbrestore::RestoreExitSuccess;
    }

    // Announced before the work starts, so a caller that reads this and does
    // not recognise the number can stop without having to spawn a second
    // process to ask.
    writeLine(usbrestore::encodeVersionLine(usbrestore::RestoreProtocolVersion));

    if (::geteuid() != 0) {
        // The GUI reaching this point would mean pkexec did not do its job. Run
        // directly, it means someone expected a tool that asks for privilege
        // rather than one that requires it.
        writeError(QStringLiteral("usb-restoration-helper must run as root. It is started by USB Restoration Tool "
                                  "through pkexec, not on its own."));
        return usbrestore::RestoreExitUsage;
    }

    usbrestore::RestoreArguments parsed;
    QString error;
    if (!usbrestore::parseRestoreArguments(arguments, &parsed, &error)) {
        writeError(error);
        return usbrestore::RestoreExitUsage;
    }

    // From here nothing the caller said is taken at face value. The guard and
    // the disk are both read fresh, and the claims that arrived in argv are
    // only ever used to refuse.
    const usbrestore::RestoreGuard guard = usbrestore::linuxRestoreGuard();

    usbrestore::DiskInfo disk;
    if (!usbrestore::refreshUsbDisk(parsed.expected.deviceId, &disk, &error)) {
        writeError(error);
        return usbrestore::RestoreExitFailed;
    }

    // The whole point of the split. A caller that asks for the system disk, a
    // disk that is not on the USB bus, or one that is mounted somewhere the
    // system depends on is refused here, with no reference to anything it said.
    if (!usbrestore::isSafeRestoreTarget(disk, guard, &error)) {
        writeError(error);
        return usbrestore::RestoreExitFailed;
    }

    // And this one catches the disk changing underneath an honest caller: the
    // polkit prompt takes seconds, which is long enough to unplug one stick and
    // plug in another. It is not a defence against a dishonest caller, who
    // could simply describe the disk accurately — that is what the check above
    // is for.
    if (!usbrestore::isSameRestoreTarget(parsed.expected, disk, &error)) {
        writeError(error);
        return usbrestore::RestoreExitFailed;
    }

    usbrestore::RestoreRequest request;
    request.disk = disk;
    request.style = parsed.style;
    request.volumeLabel = parsed.volumeLabel;
    request.guard = guard;

    HelperReporter reporter;
    usbrestore::RestoreResult result;
    if (!usbrestore::performLinuxRestore(request, reporter, &result, &error)) {
        // A restore that stopped because it was asked to reports no error. The
        // GUI has to be able to tell that apart from a failure, and guessing
        // from an empty stderr line would be guessing.
        if (reporter.cancelRequested() && error.isEmpty()) {
            return usbrestore::RestoreExitCancelled;
        }
        writeError(error.isEmpty() ? QStringLiteral("The restore failed.") : error);
        return usbrestore::RestoreExitFailed;
    }

    writeLine(usbrestore::encodeResultLine(result.location));
    return usbrestore::RestoreExitSuccess;
}

#pragma once

#include "core/disk.h"

#include <QString>
#include <QStringList>

namespace usbrestore {

// The line protocol between the unprivileged GUI and the privileged restore
// helper described in docs/polkit-helper.md. It lives in src/core because both
// ends have to agree on it byte for byte and because the argument half is a
// trust boundary: everything here is parsed from a string supplied by a process
// that is, by construction, less privileged than the one reading it.
//
// Nothing in this file grants anything. The arguments the helper receives are
// claims about a disk, and the helper's own enumeration is what decides whether
// a restore happens. Adding a field can only give the helper another reason to
// refuse.

// Bumped whenever the meaning of any argument or output line changes. The GUI
// asks the helper for its version before asking it to do anything, so a
// mismatched pair says so instead of misreading each other.
inline constexpr int RestoreProtocolVersion = 2;

// Asks the helper to print RestoreProtocolVersion and exit without touching a
// disk. The only invocation that is not a restore.
inline constexpr char ProtocolVersionFlag[] = "--protocol-version";

// What the helper's exit status means. Part of the protocol rather than an
// implementation detail of either end, because both have to read these the
// same way.
inline constexpr int RestoreExitSuccess = 0;
// The restore was refused or it failed. One line on stderr says which.
inline constexpr int RestoreExitFailed = 1;
// The two binaries disagree about their arguments, which in practice means a
// GUI and a helper from different versions.
inline constexpr int RestoreExitUsage = 2;
// Stopped because it was asked to, while stopping was still harmless. Distinct
// from a failure so the GUI does not report a cancellation as an error.
inline constexpr int RestoreExitCancelled = 3;

// The exit status in the words of the person holding the USB stick. Returns an
// empty string for RestoreExitSuccess and RestoreExitCancelled, neither of
// which is something to report as an error.
//
// It also covers 126 and 127, which are pkexec's rather than the helper's —
// authentication dismissed, and the helper could not be executed. They cannot
// collide, because the helper only ever exits 0 to 3.
QString describeRestoreExit(int exitCode, const QString &helperStderr);

// Everything the GUI observed about one helper run, gathered once the process
// has exited. `killRequested` records that a SIGKILL was *sent* because a
// cancel arrived before the version line — deliberately not whether it worked,
// because the sender cannot know: the moment pkexec authenticates and execs
// the helper, the process is root and an unprivileged parent's signal fails
// without a word. The verdict works out what actually happened from the other
// fields instead.
struct HelperRunObservation {
    // exitStatus() != NormalExit: the process was killed or crashed.
    bool crashed = false;
    int exitCode = 0;
    bool sawVersion = false;
    bool versionAccepted = false;
    bool killRequested = false;
    QString location;
    QString helperStderr;
};

enum class HelperRunVerdict {
    // The helper restored the disk and said where the volume ended up.
    Succeeded,
    // The run stopped before anything was written. Carries no error text,
    // which is how RestoreWorker tells a cancellation from a failure.
    Cancelled,
    // Everything else; `error` says what, in the user's terms.
    Failed,
};

// The one place a helper run's observations become an answer. The order of
// the checks is the point, and it is easy to get wrong inline: a kill that
// was requested is only believed to have worked when the process also died
// without ever speaking, and a version mismatch outranks the exit code,
// because the lines of a helper this build cannot parse prove nothing — its
// "cancelled" included.
HelperRunVerdict judgeHelperRun(const HelperRunObservation &run, QString *error = nullptr);

// What the GUI claims about the disk the user selected, as recovered from the
// helper's argv. Every field is an expectation to be checked against what the
// helper reads from sysfs itself, never a substitute for reading it.
struct RestoreArguments {
    DiskInfo expected;
    PartitionStyle style = PartitionStyle::Gpt;
    FileSystemType fileSystem = FileSystemType::ExFat;
    QString volumeLabel;
};

// The shape of a whole-disk device node: "/dev/" and one segment of characters
// a kernel block device name can actually contain. This only rejects the
// obviously malformed — a path that passes still has to name a directory under
// /sys/block, which is what rules out partitions and anything invented.
bool isPlausibleDeviceNodePath(const QString &path);

// An exFAT label the helper is willing to hand to mkfs. Eleven characters is
// the format's limit; the character set is narrow on purpose, and a leading
// "-" is refused so a label can never be read as an option by the tool it is
// passed to.
bool isValidVolumeLabel(const QString &label);

// The argv the GUI invokes the helper with, excluding the program name.
QStringList buildRestoreArguments(const DiskInfo &disk, PartitionStyle style, FileSystemType fileSystem,
                                  const QString &volumeLabel);

// The reverse. Returns false with a reason on anything unrecognised, repeated,
// missing or malformed: a helper that guesses at its arguments is a helper that
// erases the wrong disk.
bool parseRestoreArguments(const QStringList &arguments, RestoreArguments *parsed = nullptr,
                           QString *error = nullptr);

enum class ProtocolLineKind {
    Unknown,
    Version,
    Step,
    Detail,
    Result,
};

struct ProtocolLine {
    ProtocolLineKind kind = ProtocolLineKind::Unknown;
    int version = 0;
    int step = 0;
    int totalSteps = 0;
    // The step message, the detail text, or the restored location.
    QString text;
};

// Strips the line protocol's own delimiters out of arbitrary text: a control
// character in a message from mkfs would otherwise turn one line into two, and
// the reader would parse the remainder as a message it never sent.
QString sanitizeProtocolText(const QString &text);

QString encodeVersionLine(int version);
QString encodeStepLine(int step, int totalSteps, const QString &message);
QString encodeDetailLine(const QString &message);
QString encodeResultLine(const QString &location);

ProtocolLine parseProtocolLine(const QString &line);

// The one word the GUI writes to the helper's stdin to ask it to stop. Honoured
// only where a restore is still reversible, exactly as an in-process cancel is.
inline constexpr char CancelRequestToken[] = "cancel";

} // namespace usbrestore

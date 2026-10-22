#pragma once

#include "core/disk.h"
#include "core/restore_steps.h"

#include <QString>
#include <QStringList>

namespace usbrestore {

// Everything here is parsed from a less privileged process: arguments are claims
// the helper checks against its own enumeration, never grants (docs/polkit-helper.md).

// Bump whenever the meaning of any argument or output line changes.
inline constexpr int RestoreProtocolVersion = 4;

inline constexpr char ProtocolVersionFlag[] = "--protocol-version";

inline constexpr int RestoreExitSuccess = 0;
// Refused or failed; one line on stderr says which.
inline constexpr int RestoreExitFailed = 1;
// In practice a GUI and a helper from different versions.
inline constexpr int RestoreExitUsage = 2;
// Stopped while stopping was still harmless; not reported as an error.
inline constexpr int RestoreExitCancelled = 3;

// Empty for success and cancellation. Also covers pkexec's 126 and 127, which
// cannot collide because the helper only exits 0 to 3.
QString describeRestoreExit(int exitCode, const QString &helperStderr);

// `killRequested` means a SIGKILL was sent, not that it worked: once pkexec execs
// the helper as root, an unprivileged parent's signal fails silently.
struct HelperRunObservation {
    // exitStatus() != NormalExit
    bool crashed = false;
    int exitCode = 0;
    bool sawVersion = false;
    bool versionAccepted = false;
    // Why the version was refused, from acceptHelperAnnouncement().
    QString versionError;
    bool killRequested = false;
    QString location;
    QString helperStderr;
};

enum class HelperRunVerdict {
    Succeeded,
    // Carries no error text, which is how RestoreWorker tells it from a failure.
    Cancelled,
    Failed,
};

// The check order matters: a requested kill is only believed if the process died
// without speaking, and a version mismatch outranks the exit code.
HelperRunVerdict judgeHelperRun(const HelperRunObservation &run, QString *error = nullptr);

// Expectations to check against what the helper reads from sysfs, never a substitute.
struct RestoreArguments {
    DiskInfo expected;
    PartitionStyle style = PartitionStyle::Gpt;
    FileSystemType fileSystem = FileSystemType::ExFat;
    QString volumeLabel;
};

// Only rejects the obviously malformed; the path must still name a directory under
// /sys/block, which is what rules out partitions.
bool isPlausibleDeviceNodePath(const QString &path);

bool isValidVolumeLabel(const QString &label);

// Excludes the program name.
QStringList buildRestoreArguments(
    const DiskInfo &disk, PartitionStyle style, FileSystemType fileSystem, const QString &volumeLabel);

// Refuses anything unrecognised, repeated or missing rather than guessing.
bool parseRestoreArguments(const QStringList &arguments, RestoreArguments *parsed = nullptr, QString *error = nullptr);

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
    // Version lines only; empty or 0 when the helper did not say.
    QString appVersion;
    int firstDestructiveStep = 0;
    int step = 0;
    int totalSteps = 0;
    // The step message, the detail text, or the restored location.
    QString text;
};

// A control character from mkfs output would otherwise split one line into two,
// and the reader would parse the remainder as a message never sent.
QString sanitizeProtocolText(const QString &text);

QString encodeVersionLine(int version, const QString &appVersion, const QVector<RestoreStep> &plan);
QString encodeStepLine(int step, int totalSteps, const QString &message);
QString encodeDetailLine(const QString &message);
QString encodeResultLine(const QString &location);

ProtocolLine parseProtocolLine(const QString &line);

// The GUI numbers the helper's steps and places "safe to unplug" by its own plan,
// so a helper announcing another protocol or plan is refused.
bool acceptHelperAnnouncement(
    const ProtocolLine &announcement,
    const QString &ourAppVersion,
    const QVector<RestoreStep> &plan,
    QString *error = nullptr);

// Written to the helper's stdin; honoured only while the restore is still reversible.
inline constexpr char CancelRequestToken[] = "cancel";

} // namespace usbrestore

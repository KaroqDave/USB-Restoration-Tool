#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

namespace usbrestore {

// Appends restore progress to a log file and mirrors it to the GUI. Restores
// are destructive and unattended-looking; when one goes wrong the log is the
// only record of what the tool saw and what it did.
//
// The file lives beside the executable, so a portable copy carries its own
// history and leaves nothing behind on the machine that ran it. When that
// directory cannot be written to — an AppImage's read-only mount, an install
// under Program Files — it falls back to per-user application data rather than
// running without a log.
class Logger : public QObject {
    Q_OBJECT
  public:
    explicit Logger(QObject *parent = nullptr);

    // Written to the file and shown in the app.
    void log(const QString &message);

    // Written to the file only: detail that matters when diagnosing a failure
    // but would bury the step list on screen.
    void logFileOnly(const QString &message);

    QString path() const;
    bool isOpen() const;

    // Whether the log ended up beside the executable, as intended, or in the
    // fallback location.
    bool isPortable() const;

  signals:
    void lineWritten(const QString &message);

  private:
    void writeLine(const QString &message);
    bool openAt(const QString &directory);

    QFile m_file;
    QMutex m_mutex;
    bool m_portable = false;
};

} // namespace usbrestore

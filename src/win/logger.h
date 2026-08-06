#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

namespace usbrestore {

// Appends restore progress to a log file and mirrors it to the GUI. Restores
// are destructive and unattended-looking; when one goes wrong the log is the
// only record of what the tool saw and what it did.
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

  signals:
    void lineWritten(const QString &message);

  private:
    void writeLine(const QString &message);

    QFile m_file;
    QMutex m_mutex;
};

} // namespace usbrestore

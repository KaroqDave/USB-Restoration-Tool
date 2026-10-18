#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>

namespace usbrestore {

// Beside the executable so a portable copy carries its own log; falls back to per-user
// app data when that is read-only (AppImage mount, Program Files).
class Logger : public QObject {
    Q_OBJECT
  public:
    explicit Logger(QObject *parent = nullptr);

    void log(const QString &message);

    void logFileOnly(const QString &message);

    QString path() const;
    bool isOpen() const;

    // False when the log fell back to per-user app data.
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

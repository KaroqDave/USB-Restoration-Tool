#include "win/logger.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace usbrestore {

namespace {

// Past this the log is rolled over to a single .1 backup. A restore writes a
// couple of dozen lines, so this holds a long history without the file growing
// without bound on a machine that restores sticks all day.
constexpr qint64 MaxLogBytes = 1024 * 1024;

} // namespace

Logger::Logger(QObject *parent) : QObject(parent)
{
    // The per-user application data directory rather than Documents: this is a
    // diagnostic file, not something the user put there, and it should not
    // appear among their own documents.
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (dir.isEmpty() || !QDir().mkpath(dir)) {
        return;
    }

    const QString path = QDir(dir).filePath(QStringLiteral("usb-restoration-tool.log"));
    if (QFileInfo(path).size() > MaxLogBytes) {
        const QString rolled = path + QStringLiteral(".1");
        QFile::remove(rolled);
        QFile::rename(path, rolled);
    }

    m_file.setFileName(path);
    // A log that cannot be opened is reported by the window rather than
    // failing startup: it costs diagnosability, not the ability to restore.
    (void)m_file.open(QIODevice::Append | QIODevice::Text);
}

void Logger::log(const QString &message)
{
    writeLine(message);
    emit lineWritten(message);
}

void Logger::logFileOnly(const QString &message)
{
    writeLine(message);
}

void Logger::writeLine(const QString &message)
{
    QMutexLocker locker(&m_mutex);
    if (!m_file.isOpen()) {
        return;
    }

    QTextStream stream(&m_file);
    stream << QStringLiteral("[%1] %2").arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message)
           << Qt::endl;
    m_file.flush();
}

QString Logger::path() const
{
    return m_file.fileName();
}

bool Logger::isOpen() const
{
    return m_file.isOpen();
}

} // namespace usbrestore

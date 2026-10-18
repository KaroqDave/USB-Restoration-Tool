#include "platform/logger.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QMutexLocker>
#include <QStandardPaths>
#include <QTextStream>

namespace usbrestore {

namespace {

constexpr auto LogFileName = "usb-restoration-tool.log";

// Rolled over to a single .1 backup past this; a restore writes a few dozen lines.
constexpr qint64 MaxLogBytes = 1024 * 1024;

} // namespace

Logger::Logger(QObject *parent) : QObject(parent)
{
    if (openAt(QCoreApplication::applicationDirPath())) {
        m_portable = true;
        return;
    }

    const QString fallback = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (!fallback.isEmpty() && QDir().mkpath(fallback)) {
        openAt(fallback);
    }
}

bool Logger::openAt(const QString &directory)
{
    if (directory.isEmpty()) {
        return false;
    }

    const QString path = QDir(directory).filePath(QString::fromLatin1(LogFileName));
    if (QFileInfo(path).size() > MaxLogBytes) {
        const QString rolled = path + QStringLiteral(".1");
        QFile::remove(rolled);
        QFile::rename(path, rolled);
    }

    m_file.setFileName(path);
    if (!m_file.open(QIODevice::Append | QIODevice::Text)) {
        m_file.setFileName(QString());
        return false;
    }
    return true;
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

bool Logger::isPortable() const
{
    return m_portable;
}

} // namespace usbrestore

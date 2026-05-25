#include "win/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QMutexLocker>
#include <QStandardPaths>

namespace usbrestore {

Logger::Logger(QObject *parent)
    : QObject(parent)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const bool ignored = QDir().mkpath(dir);
    Q_UNUSED(ignored);
    m_file.setFileName(QDir(dir).filePath(QStringLiteral("USBRestorationTool.log")));
    const bool opened = m_file.open(QIODevice::Append | QIODevice::Text);
    Q_UNUSED(opened);
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
    const QString line = QStringLiteral("[%1] %2")
                             .arg(QDateTime::currentDateTime().toString(Qt::ISODateWithMs), message);
    if (m_file.isOpen()) {
        QTextStream stream(&m_file);
        stream << line << Qt::endl;
        m_file.flush();
    }
}

QString Logger::path() const
{
    return m_file.fileName();
}

}

#pragma once

#include <QFile>
#include <QMutex>
#include <QObject>
#include <QString>
#include <QTextStream>

namespace usbrestore {

class Logger : public QObject {
    Q_OBJECT
public:
    explicit Logger(QObject *parent = nullptr);
    void log(const QString &message);
    void logFileOnly(const QString &message);
    QString path() const;

signals:
    void lineWritten(const QString &message);

private:
    void writeLine(const QString &message);

    QFile m_file;
    QMutex m_mutex;
};

}

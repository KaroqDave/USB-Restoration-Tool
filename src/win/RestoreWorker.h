#pragma once

#include "win/Core.h"

#include <QObject>

namespace usbrestore {

class RestoreWorker : public QObject {
    Q_OBJECT
public:
    explicit RestoreWorker(DiskInfo disk);

public slots:
    void run();

signals:
    void progress(const QString &message);
    void logMessage(const QString &message);
    void logFileOnly(const QString &message);
    void failed(const QString &message);
    void finished(const QString &driveRoot);

private:
    void step(const QString &message);
    void fail(const QString &message);

    DiskInfo m_disk;
};

}

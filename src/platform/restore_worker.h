#pragma once

#include "platform/disk_service.h"

#include <QObject>
#include <QElapsedTimer>
#include <QString>

#include <atomic>

namespace usbrestore {

// Created on the GUI thread and moved to the thread that calls run(); results leave
// only through signals.
class RestoreWorker : public QObject, private RestoreReporter {
    Q_OBJECT
  public:
    RestoreWorker(DiskService &service, RestoreRequest request);

    // Ignored once writing has begun: a half-written partition table is worse
    // than a finished one.
    void requestCancel();

  public slots:
    void run();

  signals:
    void progress(int step, int totalSteps, const QString &message);
    void logMessage(const QString &message);
    void logFileOnly(const QString &message);
    void failed(const QString &message);
    void cancelled();
    void finished(const QString &location);

  private:
    void step(const QString &message) override;
    void detail(const QString &message) override;
    bool cancelRequested() const override;
    void logStepDuration();

    DiskService &m_service;
    RestoreRequest m_request;
    int m_step = 0;
    QElapsedTimer m_elapsed;
    qint64 m_stepStartedMs = 0;
    std::atomic_bool m_cancelRequested{false};
};

} // namespace usbrestore

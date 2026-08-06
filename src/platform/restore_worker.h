#pragma once

#include "platform/disk_service.h"

#include <QObject>
#include <QString>

#include <atomic>

namespace usbrestore {

// Runs one restore on a worker thread. The object is created on the GUI thread
// and moved to the thread that calls run(); every result leaves through a
// signal. All it does is adapt the backend's RestoreReporter calls into those
// signals, so the platform code never touches Qt's threading.
class RestoreWorker : public QObject, private RestoreReporter {
    Q_OBJECT
  public:
    RestoreWorker(DiskService &service, RestoreRequest request);

    // Asks the restore to stop. Only honoured while the work is still
    // reversible: once the first sector has been overwritten there is nothing
    // left to preserve by stopping, and a half-written partition table is
    // worse than a finished one.
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

    DiskService &m_service;
    RestoreRequest m_request;
    int m_step = 0;
    std::atomic_bool m_cancelRequested{false};
};

} // namespace usbrestore

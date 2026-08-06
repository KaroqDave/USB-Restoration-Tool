#pragma once

#include "core/disk.h"

#include <QObject>
#include <QStringList>

#include <atomic>

namespace usbrestore {

// Runs one restore on a worker thread. The object is created on the GUI thread
// and moved to the thread that calls run(); every result leaves through a
// signal.
class RestoreWorker : public QObject {
    Q_OBJECT
  public:
    // Total number of steps a successful restore reports, so the GUI can show
    // real progress instead of a bar that only knows "busy".
    static constexpr int TotalSteps = 14;

    RestoreWorker(DiskInfo disk, QStringList protectedDriveLetters, QString volumeLabel);

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
    void finished(const QString &driveRoot);

  private:
    void step(const QString &message);
    void fail(const QString &message);
    bool cancelRequested() const;

    DiskInfo m_disk;
    QStringList m_protectedDriveLetters;
    QString m_volumeLabel;
    int m_step = 0;
    std::atomic_bool m_cancelRequested{false};
};

} // namespace usbrestore

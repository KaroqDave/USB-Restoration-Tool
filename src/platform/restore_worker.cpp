#include "platform/restore_worker.h"

#include <QtGlobal>

#include <utility>

namespace usbrestore {

RestoreWorker::RestoreWorker(DiskService &service, RestoreRequest request)
    : m_service(service), m_request(std::move(request))
{
}

void RestoreWorker::requestCancel()
{
    m_cancelRequested.store(true, std::memory_order_relaxed);
}

bool RestoreWorker::cancelRequested() const
{
    return m_cancelRequested.load(std::memory_order_relaxed);
}

void RestoreWorker::run()
{
    m_elapsed.start();
    RestoreResult result;
    QString error;
    QString displayError;
    const bool restored = m_service.restore(m_request, *this, &result, &error, &displayError);
    logStepDuration();
    emit logFileOnly(QStringLiteral("Restore attempt took %1 ms").arg(m_elapsed.elapsed()));
    if (restored) {
        emit finished(result.location);
        return;
    }

    // A backend that stopped because it was asked to reports no error; that is
    // the one failure that is not a failure.
    if (cancelRequested() && error.isEmpty()) {
        emit cancelled();
        return;
    }

    const auto fallback = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "The restore failed."));
    const QString logError = error.isEmpty() ? fallback.english : error;
    const QString uiError = displayError.isEmpty() ? (error.isEmpty() ? fallback.display : error) : displayError;
    emit logMessage(QStringLiteral("ERROR: %1").arg(logError));
    emit failed(uiError);
}

void RestoreWorker::step(const RestoreMessage &message)
{
    logStepDuration();
    m_step = qMin(m_step + 1, m_service.totalRestoreSteps());
    emit logMessage(message.english);
    emit progress(m_step, m_service.totalRestoreSteps(), message.display);
}

void RestoreWorker::logStepDuration()
{
    const qint64 now = m_elapsed.elapsed();
    if (m_step == 0) {
        emit logFileOnly(QStringLiteral("Restore setup took %1 ms").arg(now));
    } else {
        emit logFileOnly(QStringLiteral("Step %1 took %2 ms").arg(m_step).arg(now - m_stepStartedMs));
    }
    m_stepStartedMs = now;
}

void RestoreWorker::detail(const QString &message)
{
    emit logFileOnly(message);
}

} // namespace usbrestore

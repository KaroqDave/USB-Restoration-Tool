#pragma once

#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>

namespace usbrestore {

// Return value the MSFT_Storage classes use for "the work was accepted and is
// running as a job". It is not a completion: the caller has to wait for the
// job before the disk actually looks the way the call asked for.
inline constexpr quint32 WmiMethodStartedJob = 4096;

// Sets the process-wide COM authentication level once, before any interface is
// marshalled. Called from main() so that every later WMI proxy inherits packet
// privacy rather than the machine default.
bool initializeComSecurity(QString *error = nullptr);

class WmiObject {
  public:
    WmiObject() = default;
    explicit WmiObject(IWbemClassObject *object);
    WmiObject(const WmiObject &other);
    WmiObject &operator=(const WmiObject &other);
    WmiObject(WmiObject &&other) noexcept;
    WmiObject &operator=(WmiObject &&other) noexcept;
    ~WmiObject();

    bool isValid() const;
    IWbemClassObject *get() const;

    QString stringValue(const wchar_t *name) const;
    quint32 uintValue(const wchar_t *name, quint32 fallback = 0) const;
    quint64 uint64Value(const wchar_t *name, quint64 fallback = 0) const;
    bool boolValue(const wchar_t *name, bool fallback = false) const;
    QString objectPath() const;

  private:
    QVariant value(const wchar_t *name) const;

    IWbemClassObject *m_object = nullptr;
};

struct WmiCallResult {
    quint32 returnValue = UINT_MAX;
    // Set when returnValue is WmiMethodStartedJob: the MSFT_StorageJob to wait
    // on. Empty when Windows completed the call synchronously.
    QString jobPath;
    QString extendedStatus;

    bool accepted() const { return returnValue == 0 || returnValue == WmiMethodStartedJob; }
};

class WmiConnection {
  public:
    explicit WmiConnection(const wchar_t *namespacePath = L"ROOT\\Microsoft\\Windows\\Storage");
    ~WmiConnection();

    WmiConnection(const WmiConnection &) = delete;
    WmiConnection &operator=(const WmiConnection &) = delete;

    bool isValid() const;
    QString lastError() const;

    QVector<WmiObject> query(const QString &wql) const;
    WmiObject getObject(const QString &objectPath) const;

    WmiCallResult callMethod(const QString &className,
                             const QString &objectPath,
                             const wchar_t *methodName,
                             const QVector<QPair<QString, QVariant>> &inputs = {}) const;

    // Runs a method and, when Windows answered with a storage job, waits for
    // that job to finish. Returns false with a filled-in error on any failure.
    bool callMethodAndWait(const QString &className,
                           const QString &objectPath,
                           const wchar_t *methodName,
                           const QVector<QPair<QString, QVariant>> &inputs,
                           int timeoutMs,
                           QString *error) const;

    bool waitForJob(const QString &jobPath, int timeoutMs, QString *error) const;

  private:
    void setError(HRESULT result, const QString &context) const;

    IWbemLocator *m_locator = nullptr;
    IWbemServices *m_services = nullptr;
    mutable QString m_lastError;
    bool m_comInitialized = false;
};

} // namespace usbrestore

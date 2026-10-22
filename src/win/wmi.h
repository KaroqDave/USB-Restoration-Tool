#pragma once

#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>

#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>

#include <optional>

namespace usbrestore {

// MSFT_Storage "accepted, running as a job": not a completion, the job must be waited for.
inline constexpr quint32 WmiMethodStartedJob = 4096;

// Call before any interface is marshalled so every WMI proxy uses packet privacy:
// the traffic names disks about to be erased.
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
    // Empty when the property is missing or not a number, so it cannot be mistaken for a real 0.
    std::optional<quint64> optionalUint64Value(const wchar_t *name) const;
    bool boolValue(const wchar_t *name, bool fallback = false) const;
    QString objectPath() const;

    // Changes this local object; method execution is a separate operation.
    bool setValue(const QString &name, const QVariant &value, QString *error = nullptr);

  private:
    QVariant value(const wchar_t *name) const;

    IWbemClassObject *m_object = nullptr;
};

struct WmiCallResult {
    quint32 returnValue = UINT_MAX;
    // Empty when Windows completed the call synchronously.
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

    // Also waits for the storage job when Windows only queued the work.
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

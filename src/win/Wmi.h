#pragma once

#include <QPair>
#include <QString>
#include <QVariant>
#include <QVector>
#include <Windows.h>
#include <Wbemidl.h>
#include <comdef.h>

namespace usbrestore {

class WmiObject {
public:
    WmiObject() = default;
    explicit WmiObject(IWbemClassObject *object);
    WmiObject(const WmiObject &other);
    WmiObject &operator=(const WmiObject &other);
    WmiObject(WmiObject &&other) noexcept;
    WmiObject &operator=(WmiObject &&other) noexcept;
    ~WmiObject();

    IWbemClassObject *get() const;
    QString stringValue(const wchar_t *name) const;
    quint32 uintValue(const wchar_t *name, quint32 fallback = 0) const;
    quint64 uint64Value(const wchar_t *name, quint64 fallback = 0) const;
    bool boolValue(const wchar_t *name, bool fallback = false) const;
    QString objectPath() const;

private:
    IWbemClassObject *m_object = nullptr;
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
    quint32 callMethod(const QString &className,
                       const QString &objectPath,
                       const wchar_t *methodName,
                       const QVector<QPair<QString, QVariant>> &inputs = {}) const;

private:
    void setError(HRESULT result, const QString &context) const;

    IWbemLocator *m_locator = nullptr;
    IWbemServices *m_services = nullptr;
    mutable QString m_lastError;
    bool m_comInitialized = false;
};

}

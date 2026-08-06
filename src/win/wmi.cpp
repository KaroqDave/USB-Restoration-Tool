#include "win/wmi.h"

#include <QElapsedTimer>
#include <QThread>
#include <QVariant>

namespace usbrestore {

namespace {

// FormatMessage has no text for the WBEM facility, so _com_error on a storage
// failure would otherwise read "Unknown error". These are the codes this tool
// can realistically provoke.
QString wbemStatusText(HRESULT result)
{
    switch (static_cast<quint32>(result)) {
    case 0x80041001:
        return QStringLiteral("WMI reported a general failure");
    case 0x80041002:
        return QStringLiteral("WMI object not found");
    case 0x80041003:
        return QStringLiteral("Access denied by WMI");
    case 0x80041008:
        return QStringLiteral("Invalid parameter passed to WMI");
    case 0x8004100E:
        return QStringLiteral("Invalid WMI namespace");
    case 0x80041010:
        return QStringLiteral("Invalid WMI class");
    case 0x80041017:
        return QStringLiteral("Invalid WMI query");
    case 0x80041021:
        return QStringLiteral("The WMI provider is not available");
    case 0x80041033:
        return QStringLiteral("The WMI provider is shutting down");
    default:
        return {};
    }
}

QString hresultMessage(HRESULT result)
{
    const QString wbem = wbemStatusText(result);
    if (!wbem.isEmpty()) {
        return QStringLiteral("%1 (HRESULT 0x%2)").arg(wbem).arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
    }

    const _com_error error(result);
    return QStringLiteral("%1 (HRESULT 0x%2)")
        .arg(QString::fromWCharArray(error.ErrorMessage()).trimmed())
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

// The storage classes are not consistent about VARIANT types: BusType,
// PartitionStyle and HealthStatus arrive as uint16, Size as a BSTR holding a
// decimal uint64, and the flags as VT_BOOL. Converting only VT_BSTR/VT_BOOL/
// VT_I4/VT_UI4 — as the first version of this file did — silently turned every
// uint16 property into its fallback, which is why BusType always read as 0.
QVariant variantToQt(const VARIANT &variant)
{
    switch (variant.vt) {
    case VT_EMPTY:
    case VT_NULL:
        return {};
    case VT_BSTR:
        return QString::fromWCharArray(variant.bstrVal);
    case VT_BOOL:
        return variant.boolVal == VARIANT_TRUE;
    case VT_I1:
        return static_cast<qint32>(variant.cVal);
    case VT_UI1:
        return static_cast<quint32>(variant.bVal);
    case VT_I2:
        return static_cast<qint32>(variant.iVal);
    case VT_UI2:
        return static_cast<quint32>(variant.uiVal);
    case VT_I4:
    case VT_INT:
        return static_cast<qint32>(variant.lVal);
    case VT_UI4:
    case VT_UINT:
        return static_cast<quint32>(variant.ulVal);
    case VT_I8:
        return static_cast<qlonglong>(variant.llVal);
    case VT_UI8:
        return static_cast<qulonglong>(variant.ullVal);
    case VT_R4:
        return static_cast<double>(variant.fltVal);
    case VT_R8:
        return variant.dblVal;
    default:
        return {};
    }
}

HRESULT putInputValue(IWbemClassObject *input, const QString &name, const QVariant &value)
{
    _variant_t variant;
    switch (value.metaType().id()) {
    case QMetaType::Bool:
        variant = _variant_t(value.toBool());
        break;
    case QMetaType::QString:
        variant = _variant_t(value.toString().toStdWString().c_str());
        break;
    case QMetaType::UInt:
    case QMetaType::ULong:
        variant = _variant_t(static_cast<unsigned long>(value.toUInt()));
        break;
    case QMetaType::Int:
    case QMetaType::Long:
        variant = _variant_t(static_cast<long>(value.toInt()));
        break;
    case QMetaType::ULongLong:
        variant = _variant_t(static_cast<ULONGLONG>(value.toULongLong()));
        break;
    case QMetaType::LongLong:
        variant = _variant_t(static_cast<LONGLONG>(value.toLongLong()));
        break;
    default:
        return E_INVALIDARG;
    }

    return input->Put(name.toStdWString().c_str(), 0, &variant, 0);
}

// MSFT_StorageJob inherits CIM_ConcreteJob's JobState values.
constexpr quint32 JobStateCompleted = 7;
constexpr quint32 JobStateTerminated = 8;
constexpr quint32 JobStateKilled = 9;
constexpr quint32 JobStateException = 10;

} // namespace

bool initializeComSecurity(QString *error)
{
    const HRESULT result = CoInitializeSecurity(nullptr,
                                                -1,
                                                nullptr,
                                                nullptr,
                                                // Packet privacy rather than the
                                                // machine default: the WMI traffic
                                                // this tool exchanges names disks
                                                // it is about to erase.
                                                RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                                                RPC_C_IMP_LEVEL_IMPERSONATE,
                                                nullptr,
                                                EOAC_NONE,
                                                nullptr);
    // RPC_E_TOO_LATE means something already set the process security, which is
    // a valid state to run in and not worth failing startup over.
    if (FAILED(result) && result != RPC_E_TOO_LATE) {
        if (error) {
            *error = QStringLiteral("CoInitializeSecurity failed: %1").arg(hresultMessage(result));
        }
        return false;
    }
    return true;
}

WmiObject::WmiObject(IWbemClassObject *object) : m_object(object) {}

WmiObject::WmiObject(const WmiObject &other) : m_object(other.m_object)
{
    if (m_object) {
        m_object->AddRef();
    }
}

WmiObject &WmiObject::operator=(const WmiObject &other)
{
    if (this == &other) {
        return *this;
    }
    if (other.m_object) {
        other.m_object->AddRef();
    }
    if (m_object) {
        m_object->Release();
    }
    m_object = other.m_object;
    return *this;
}

WmiObject::WmiObject(WmiObject &&other) noexcept : m_object(other.m_object)
{
    other.m_object = nullptr;
}

WmiObject &WmiObject::operator=(WmiObject &&other) noexcept
{
    if (this == &other) {
        return *this;
    }
    if (m_object) {
        m_object->Release();
    }
    m_object = other.m_object;
    other.m_object = nullptr;
    return *this;
}

WmiObject::~WmiObject()
{
    if (m_object) {
        m_object->Release();
    }
}

bool WmiObject::isValid() const
{
    return m_object != nullptr;
}

IWbemClassObject *WmiObject::get() const
{
    return m_object;
}

QVariant WmiObject::value(const wchar_t *name) const
{
    if (!m_object) {
        return {};
    }

    VARIANT variant;
    VariantInit(&variant);
    if (FAILED(m_object->Get(name, 0, &variant, nullptr, nullptr))) {
        return {};
    }
    const QVariant result = variantToQt(variant);
    VariantClear(&variant);
    return result;
}

QString WmiObject::stringValue(const wchar_t *name) const
{
    return value(name).toString();
}

quint32 WmiObject::uintValue(const wchar_t *name, quint32 fallback) const
{
    const QVariant result = value(name);
    if (!result.isValid()) {
        return fallback;
    }
    bool ok = false;
    const quint32 converted = result.toUInt(&ok);
    return ok ? converted : fallback;
}

quint64 WmiObject::uint64Value(const wchar_t *name, quint64 fallback) const
{
    const QVariant result = value(name);
    if (!result.isValid()) {
        return fallback;
    }
    bool ok = false;
    const quint64 converted = result.toULongLong(&ok);
    return ok ? converted : fallback;
}

bool WmiObject::boolValue(const wchar_t *name, bool fallback) const
{
    const QVariant result = value(name);
    return result.isValid() ? result.toBool() : fallback;
}

QString WmiObject::objectPath() const
{
    return stringValue(L"__PATH");
}

WmiConnection::WmiConnection(const wchar_t *namespacePath)
{
    // Multi-threaded rather than apartment-threaded. WMI calls are marshalled
    // to the WMI service, and a cross-apartment call from an STA thread only
    // returns while that thread pumps messages — which the restore worker
    // thread does not do. On the GUI thread Qt has already entered an STA, so
    // this returns RPC_E_CHANGED_MODE and the existing apartment is used.
    HRESULT result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        setError(result, QStringLiteral("CoInitializeEx failed"));
        return;
    }
    m_comInitialized = SUCCEEDED(result);

    result = CoCreateInstance(
        CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, reinterpret_cast<void **>(&m_locator));
    if (FAILED(result)) {
        setError(result, QStringLiteral("CoCreateInstance(IWbemLocator) failed"));
        return;
    }

    result =
        m_locator->ConnectServer(_bstr_t(namespacePath), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &m_services);
    if (FAILED(result)) {
        setError(result, QStringLiteral("WMI ConnectServer failed"));
        m_services = nullptr;
        return;
    }

    result = CoSetProxyBlanket(m_services,
                               RPC_C_AUTHN_WINNT,
                               RPC_C_AUTHZ_NONE,
                               nullptr,
                               RPC_C_AUTHN_LEVEL_PKT_PRIVACY,
                               RPC_C_IMP_LEVEL_IMPERSONATE,
                               nullptr,
                               EOAC_NONE);
    if (FAILED(result)) {
        setError(result, QStringLiteral("CoSetProxyBlanket failed"));
        m_services->Release();
        m_services = nullptr;
    }
}

WmiConnection::~WmiConnection()
{
    if (m_services) {
        m_services->Release();
    }
    if (m_locator) {
        m_locator->Release();
    }
    if (m_comInitialized) {
        CoUninitialize();
    }
}

bool WmiConnection::isValid() const
{
    return m_services != nullptr;
}

QString WmiConnection::lastError() const
{
    return m_lastError;
}

QVector<WmiObject> WmiConnection::query(const QString &wql) const
{
    QVector<WmiObject> objects;
    if (!m_services) {
        return objects;
    }
    m_lastError.clear();

    IEnumWbemClassObject *enumerator = nullptr;
    HRESULT result = m_services->ExecQuery(_bstr_t(L"WQL"),
                                           _bstr_t(wql.toStdWString().c_str()),
                                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                           nullptr,
                                           &enumerator);
    if (FAILED(result) || !enumerator) {
        setError(FAILED(result) ? result : E_FAIL, QStringLiteral("WMI query failed: %1").arg(wql));
        return objects;
    }

    for (;;) {
        IWbemClassObject *object = nullptr;
        ULONG returned = 0;
        result = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
        if (FAILED(result)) {
            setError(result, QStringLiteral("WMI result enumeration failed: %1").arg(wql));
            break;
        }
        if (returned == 0 || !object) {
            break;
        }
        objects.push_back(WmiObject(object));
    }

    enumerator->Release();
    return objects;
}

WmiObject WmiConnection::getObject(const QString &objectPath) const
{
    if (!m_services || objectPath.isEmpty()) {
        return {};
    }
    m_lastError.clear();

    IWbemClassObject *object = nullptr;
    const HRESULT result =
        m_services->GetObject(_bstr_t(objectPath.toStdWString().c_str()), 0, nullptr, &object, nullptr);
    if (FAILED(result) || !object) {
        setError(FAILED(result) ? result : E_FAIL, QStringLiteral("WMI GetObject failed for %1").arg(objectPath));
        return {};
    }
    return WmiObject(object);
}

WmiCallResult WmiConnection::callMethod(const QString &className,
                                        const QString &objectPath,
                                        const wchar_t *methodName,
                                        const QVector<QPair<QString, QVariant>> &inputs) const
{
    WmiCallResult callResult;
    if (!m_services) {
        return callResult;
    }
    m_lastError.clear();

    IWbemClassObject *classObject = nullptr;
    IWbemClassObject *inSignature = nullptr;
    IWbemClassObject *inInstance = nullptr;
    IWbemClassObject *outParams = nullptr;

    HRESULT result =
        m_services->GetObject(_bstr_t(className.toStdWString().c_str()), 0, nullptr, &classObject, nullptr);
    if (FAILED(result)) {
        setError(result, QStringLiteral("WMI GetObject failed for class %1").arg(className));
    } else {
        result = classObject->GetMethod(methodName, 0, &inSignature, nullptr);
        if (FAILED(result)) {
            setError(result,
                     QStringLiteral("WMI GetMethod failed for %1.%2")
                         .arg(className, QString::fromWCharArray(methodName)));
        }
    }

    if (SUCCEEDED(result) && inSignature) {
        result = inSignature->SpawnInstance(0, &inInstance);
        if (FAILED(result) || !inInstance) {
            result = FAILED(result) ? result : E_FAIL;
            setError(result, QStringLiteral("WMI input parameter object creation failed"));
        }
    } else if (SUCCEEDED(result) && !inputs.isEmpty()) {
        result = E_INVALIDARG;
        setError(result,
                 QStringLiteral("WMI method has no input parameter signature: %1")
                     .arg(QString::fromWCharArray(methodName)));
    }

    if (SUCCEEDED(result) && inInstance) {
        for (const auto &input : inputs) {
            result = putInputValue(inInstance, input.first, input.second);
            if (FAILED(result)) {
                setError(result, QStringLiteral("WMI input parameter failed: %1").arg(input.first));
                break;
            }
        }
    }

    if (SUCCEEDED(result)) {
        result = m_services->ExecMethod(_bstr_t(objectPath.toStdWString().c_str()),
                                        _bstr_t(methodName),
                                        0,
                                        nullptr,
                                        inInstance,
                                        &outParams,
                                        nullptr);
        if (FAILED(result)) {
            setError(result,
                     QStringLiteral("WMI ExecMethod failed for %1.%2")
                         .arg(className, QString::fromWCharArray(methodName)));
        }
    }

    if (SUCCEEDED(result)) {
        if (outParams) {
            const WmiObject output(outParams);
            outParams = nullptr;
            callResult.returnValue = output.uintValue(L"ReturnValue", 0);
            callResult.jobPath = output.stringValue(L"CreatedStorageJob");
            callResult.extendedStatus = output.stringValue(L"ExtendedStatus");
        } else {
            callResult.returnValue = 0;
        }
    }

    if (inInstance) {
        inInstance->Release();
    }
    if (inSignature) {
        inSignature->Release();
    }
    if (classObject) {
        classObject->Release();
    }
    if (outParams) {
        outParams->Release();
    }
    return callResult;
}

bool WmiConnection::waitForJob(const QString &jobPath, int timeoutMs, QString *error) const
{
    if (jobPath.isEmpty()) {
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    for (;;) {
        const WmiObject job = getObject(jobPath);
        if (!job.isValid()) {
            // The job object disappears once Windows has cleaned it up, which
            // it only does after the job has run. Treat that as completion
            // rather than failing a restore that already succeeded.
            return true;
        }

        const quint32 state = job.uintValue(L"JobState", JobStateCompleted);
        if (state == JobStateCompleted) {
            return true;
        }
        if (state == JobStateTerminated || state == JobStateKilled || state == JobStateException) {
            if (error) {
                const QString detail = job.stringValue(L"ErrorDescription").trimmed();
                *error = detail.isEmpty()
                             ? QStringLiteral("A Windows storage job failed with state %1.").arg(state)
                             : QStringLiteral("A Windows storage job failed: %1").arg(detail);
            }
            return false;
        }

        if (timer.elapsed() >= timeoutMs) {
            if (error) {
                *error = QStringLiteral("A Windows storage job did not finish within %1 seconds.").arg(timeoutMs / 1000);
            }
            return false;
        }
        QThread::msleep(250);
    }
}

bool WmiConnection::callMethodAndWait(const QString &className,
                                      const QString &objectPath,
                                      const wchar_t *methodName,
                                      const QVector<QPair<QString, QVariant>> &inputs,
                                      int timeoutMs,
                                      QString *error) const
{
    const WmiCallResult result = callMethod(className, objectPath, methodName, inputs);
    if (!result.accepted()) {
        if (error) {
            const QString method = QStringLiteral("%1.%2").arg(className, QString::fromWCharArray(methodName));
            QString detail = lastError();
            if (detail.isEmpty()) {
                detail = result.extendedStatus.trimmed();
            }
            *error = detail.isEmpty()
                         ? QStringLiteral("%1 failed with return value %2.").arg(method).arg(result.returnValue)
                         : QStringLiteral("%1 failed with return value %2. %3")
                               .arg(method)
                               .arg(result.returnValue)
                               .arg(detail);
        }
        return false;
    }

    // A 4096 means Windows only queued the work. Returning here without
    // waiting is what made the next step race the one before it.
    return waitForJob(result.jobPath, timeoutMs, error);
}

void WmiConnection::setError(HRESULT result, const QString &context) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, hresultMessage(result));
}

} // namespace usbrestore

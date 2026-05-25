#include "win/Wmi.h"

#include <QVariant>

namespace usbrestore {

namespace {

QString hresultMessage(HRESULT result)
{
    _com_error error(result);
    return QStringLiteral("%1 (HRESULT 0x%2)")
        .arg(QString::fromWCharArray(error.ErrorMessage()))
        .arg(static_cast<quint32>(result), 8, 16, QLatin1Char('0'));
}

QVariant variantToQt(const VARIANT &variant)
{
    switch (variant.vt) {
    case VT_BSTR:
        return QString::fromWCharArray(variant.bstrVal);
    case VT_BOOL:
        return variant.boolVal == VARIANT_TRUE;
    case VT_I4:
    case VT_INT:
        return static_cast<qint32>(variant.intVal);
    case VT_UI4:
    case VT_UINT:
        return static_cast<quint32>(variant.uintVal);
    case VT_I8:
        return static_cast<qlonglong>(variant.llVal);
    case VT_UI8:
        return static_cast<qulonglong>(variant.ullVal);
    default:
        return {};
    }
}

HRESULT putInputValue(IWbemClassObject *input, const QString &name, const QVariant &value)
{
    _variant_t variant;
    const int type = value.metaType().id();

    if (type == QMetaType::Bool) {
        variant = value.toBool() ? VARIANT_TRUE : VARIANT_FALSE;
        variant.vt = VT_BOOL;
    } else if (type == QMetaType::QString) {
        variant = _bstr_t(value.toString().toStdWString().c_str());
    } else if (type == QMetaType::UInt || type == QMetaType::ULong) {
        variant = static_cast<ULONG>(value.toUInt());
        variant.vt = VT_UI4;
    } else if (type == QMetaType::Int || type == QMetaType::Long) {
        variant = static_cast<LONG>(value.toInt());
        variant.vt = VT_I4;
    } else if (type == QMetaType::ULongLong) {
        variant = static_cast<ULONGLONG>(value.toULongLong());
        variant.vt = VT_UI8;
    } else if (type == QMetaType::LongLong) {
        variant = static_cast<LONGLONG>(value.toLongLong());
        variant.vt = VT_I8;
    } else {
        return E_INVALIDARG;
    }

    return input->Put(name.toStdWString().c_str(), 0, &variant, 0);
}

}

WmiObject::WmiObject(IWbemClassObject *object)
    : m_object(object)
{
}

WmiObject::WmiObject(const WmiObject &other)
    : m_object(other.m_object)
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
    if (m_object) {
        m_object->Release();
    }
    m_object = other.m_object;
    if (m_object) {
        m_object->AddRef();
    }
    return *this;
}

WmiObject::WmiObject(WmiObject &&other) noexcept
    : m_object(other.m_object)
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

IWbemClassObject *WmiObject::get() const
{
    return m_object;
}

QString WmiObject::stringValue(const wchar_t *name) const
{
    if (!m_object) {
        return {};
    }
    VARIANT value;
    VariantInit(&value);
    const HRESULT result = m_object->Get(name, 0, &value, nullptr, nullptr);
    if (FAILED(result)) {
        return {};
    }
    const QVariant qtValue = variantToQt(value);
    VariantClear(&value);
    return qtValue.toString();
}

quint32 WmiObject::uintValue(const wchar_t *name, quint32 fallback) const
{
    if (!m_object) {
        return fallback;
    }
    VARIANT value;
    VariantInit(&value);
    if (FAILED(m_object->Get(name, 0, &value, nullptr, nullptr))) {
        return fallback;
    }
    const QVariant qtValue = variantToQt(value);
    VariantClear(&value);
    return qtValue.isValid() ? qtValue.toUInt() : fallback;
}

quint64 WmiObject::uint64Value(const wchar_t *name, quint64 fallback) const
{
    if (!m_object) {
        return fallback;
    }
    VARIANT value;
    VariantInit(&value);
    if (FAILED(m_object->Get(name, 0, &value, nullptr, nullptr))) {
        return fallback;
    }
    const QVariant qtValue = variantToQt(value);
    VariantClear(&value);
    return qtValue.isValid() ? qtValue.toULongLong() : fallback;
}

bool WmiObject::boolValue(const wchar_t *name, bool fallback) const
{
    if (!m_object) {
        return fallback;
    }
    VARIANT value;
    VariantInit(&value);
    if (FAILED(m_object->Get(name, 0, &value, nullptr, nullptr))) {
        return fallback;
    }
    const QVariant qtValue = variantToQt(value);
    VariantClear(&value);
    return qtValue.isValid() ? qtValue.toBool() : fallback;
}

QString WmiObject::objectPath() const
{
    return stringValue(L"__PATH");
}

WmiConnection::WmiConnection(const wchar_t *namespacePath)
{
    HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    if (FAILED(result) && result != RPC_E_CHANGED_MODE) {
        setError(result, QStringLiteral("CoInitializeEx failed"));
        return;
    }
    m_comInitialized = SUCCEEDED(result);

    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT,
                         RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);

    result = CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator,
                              reinterpret_cast<void **>(&m_locator));
    if (FAILED(result)) {
        setError(result, QStringLiteral("CoCreateInstance(IWbemLocator) failed"));
        return;
    }

    result = m_locator->ConnectServer(_bstr_t(namespacePath), nullptr, nullptr, nullptr, 0, nullptr, nullptr, &m_services);
    if (FAILED(result)) {
        setError(result, QStringLiteral("WMI ConnectServer failed"));
        return;
    }

    result = CoSetProxyBlanket(m_services, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr,
                               RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    if (FAILED(result)) {
        setError(result, QStringLiteral("CoSetProxyBlanket failed"));
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
    HRESULT result = m_services->ExecQuery(_bstr_t(L"WQL"), _bstr_t(wql.toStdWString().c_str()),
                                           WBEM_FLAG_FORWARD_ONLY | WBEM_FLAG_RETURN_IMMEDIATELY,
                                           nullptr, &enumerator);
    if (FAILED(result)) {
        setError(result, QStringLiteral("WMI query failed: %1").arg(wql));
        return objects;
    }

    while (enumerator) {
        IWbemClassObject *object = nullptr;
        ULONG returned = 0;
        result = enumerator->Next(WBEM_INFINITE, 1, &object, &returned);
        if (FAILED(result) || returned == 0) {
            break;
        }
        objects.push_back(WmiObject(object));
    }

    if (enumerator) {
        enumerator->Release();
    }
    return objects;
}

quint32 WmiConnection::callMethod(const QString &className,
                                  const QString &objectPath,
                                  const wchar_t *methodName,
                                  const QVector<QPair<QString, QVariant>> &inputs) const
{
    if (!m_services) {
        return UINT_MAX;
    }
    m_lastError.clear();

    IWbemClassObject *classObject = nullptr;
    IWbemClassObject *inSignature = nullptr;
    IWbemClassObject *inInstance = nullptr;
    IWbemClassObject *outParams = nullptr;

    HRESULT result = m_services->GetObject(_bstr_t(className.toStdWString().c_str()), 0, nullptr, &classObject, nullptr);
    if (FAILED(result)) {
        setError(result, QStringLiteral("WMI GetObject failed for class %1").arg(className));
    } else {
        result = classObject->GetMethod(methodName, 0, &inSignature, nullptr);
        if (FAILED(result)) {
            setError(result, QStringLiteral("WMI GetMethod failed for %1.%2").arg(className, QString::fromWCharArray(methodName)));
        }
    }

    if (SUCCEEDED(result) && inSignature) {
        result = inSignature->SpawnInstance(0, &inInstance);
        if (FAILED(result) || !inInstance) {
            setError(FAILED(result) ? result : E_FAIL, QStringLiteral("WMI input parameter object creation failed"));
        }
    } else if (SUCCEEDED(result) && !inputs.isEmpty()) {
        result = E_INVALIDARG;
        setError(result, QStringLiteral("WMI method has no input parameter signature: %1").arg(QString::fromWCharArray(methodName)));
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
        result = m_services->ExecMethod(_bstr_t(objectPath.toStdWString().c_str()), _bstr_t(methodName),
                                        0, nullptr, inInstance, &outParams, nullptr);
        if (FAILED(result)) {
            setError(result, QStringLiteral("WMI ExecMethod failed"));
        }
    }

    quint32 returnValue = UINT_MAX;
    if (SUCCEEDED(result) && outParams) {
        WmiObject output(outParams);
        outParams = nullptr;
        returnValue = output.uintValue(L"ReturnValue", 0);
    } else if (SUCCEEDED(result)) {
        returnValue = 0;
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
    return returnValue;
}

void WmiConnection::setError(HRESULT result, const QString &context) const
{
    m_lastError = QStringLiteral("%1: %2").arg(context, hresultMessage(result));
}

}

#include "win/wmi.h"

#include <QtTest/QtTest>

#include <limits>

using namespace usbrestore;

namespace {

// An in-memory class exercises WMI coercion without a storage provider or a disk.
WmiObject inputInstance(CIMTYPE type)
{
    IWbemClassObject *rawClass = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_WbemClassObject,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemClassObject,
            reinterpret_cast<void **>(&rawClass)))) {
        return {};
    }
    const WmiObject inputClass(rawClass);
    _variant_t name(L"UsbRestoreTestInputs");
    if (FAILED(rawClass->Put(L"__CLASS", 0, &name, 0))) {
        return {};
    }
    _variant_t empty;
    empty.vt = VT_NULL;
    if (FAILED(rawClass->Put(L"Value", 0, &empty, type))) {
        return {};
    }
    IWbemClassObject *instance = nullptr;
    if (FAILED(rawClass->SpawnInstance(0, &instance))) {
        return {};
    }
    return WmiObject(instance);
}

} // namespace

class WmiTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        m_comInitialized = SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED));
        QVERIFY(m_comInitialized);
    }

    void cleanupTestCase()
    {
        if (m_comInitialized) {
            CoUninitialize();
        }
    }

    void acceptsUnsignedFormatAllocationUnitSizes_data()
    {
        QTest::addColumn<QVariant>("value");
        for (quint32 size : {512u, 1024u, 2048u, 4096u, 8192u, 16384u, 32768u, 65536u, 131072u, 262144u}) {
            QTest::newRow(qPrintable(QString::number(size))) << QVariant(size);
        }
        QTest::newRow("unsigned-long") << QVariant::fromValue(8192ul);
        QTest::newRow("high-bit") << QVariant(0x80000000u);
        QTest::newRow("maximum-uint32") << QVariant(std::numeric_limits<quint32>::max());
    }

    void acceptsUnsignedFormatAllocationUnitSizes()
    {
        QFETCH(QVariant, value);
        WmiObject input = inputInstance(CIM_UINT32);
        QVERIFY(input.isValid());
        QString error;
        QVERIFY2(input.setValue(QStringLiteral("Value"), value, &error), qPrintable(error));

        _variant_t result;
        CIMTYPE type = 0;
        QVERIFY(SUCCEEDED(input.get()->Get(L"Value", 0, &result, &type, nullptr)));
        QCOMPARE(type, CIMTYPE(CIM_UINT32));
        QCOMPARE(result.vt, VARTYPE(VT_I4));
        QCOMPARE(static_cast<quint32>(result.lVal), value.toUInt());
    }

    void encodes64BitNumbersAsDecimalStrings_data()
    {
        QTest::addColumn<QVariant>("value");
        QTest::addColumn<int>("type");
        QTest::addColumn<QString>("expected");
        QTest::newRow("uint64") << QVariant::fromValue(std::numeric_limits<quint64>::max()) << int(CIM_UINT64)
                                << QStringLiteral("18446744073709551615");
        QTest::newRow("sint64") << QVariant::fromValue(std::numeric_limits<qint64>::min()) << int(CIM_SINT64)
                                << QStringLiteral("-9223372036854775808");
    }

    void encodes64BitNumbersAsDecimalStrings()
    {
        QFETCH(QVariant, value);
        QFETCH(int, type);
        QFETCH(QString, expected);
        WmiObject input = inputInstance(type);
        QVERIFY(input.isValid());
        QString error;
        QVERIFY2(input.setValue(QStringLiteral("Value"), value, &error), qPrintable(error));
        QCOMPARE(input.stringValue(L"Value"), expected);
    }

    void preservesOtherFormatParameterTypes()
    {
        WmiObject text = inputInstance(CIM_STRING);
        WmiObject flag = inputInstance(CIM_BOOLEAN);
        WmiObject number = inputInstance(CIM_SINT32);
        QVERIFY(text.isValid() && flag.isValid() && number.isValid());
        QVERIFY(text.setValue(QStringLiteral("Value"), QStringLiteral("FAT32")));
        QCOMPARE(text.stringValue(L"Value"), QStringLiteral("FAT32"));
        QVERIFY(flag.setValue(QStringLiteral("Value"), true));
        QVERIFY(flag.boolValue(L"Value"));
        QVERIFY(flag.setValue(QStringLiteral("Value"), false));
        QVERIFY(!flag.boolValue(L"Value", true));
        QVERIFY(number.setValue(QStringLiteral("Value"), -42));
        QCOMPARE(number.stringValue(L"Value"), QStringLiteral("-42"));
    }

    void describesParameterTypeMismatches()
    {
        WmiObject input = inputInstance(CIM_UINT32);
        QVERIFY(input.isValid());
        QString error;
        QVERIFY(!input.setValue(QStringLiteral("Value"), QStringLiteral("not a number"), &error));
        QVERIFY(error.contains(QStringLiteral("Value")));
        QVERIFY(error.contains(QStringLiteral("WMI parameter type mismatch")));
        QVERIFY(error.contains(QStringLiteral("0x80041005")));
    }

  private:
    bool m_comInitialized = false;
};

QTEST_GUILESS_MAIN(WmiTests)
#include "test_wmi.moc"

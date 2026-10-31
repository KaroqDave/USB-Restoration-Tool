#include "win/wmi.h"
#include "win/windows_disk_service.h"
#include "win/raw_disk.h"
#include "win/windows_util.h"

#include <QElapsedTimer>
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

WmiObject storageJob(std::optional<quint32> state, std::optional<quint32> errorCode)
{
    IWbemClassObject *raw = nullptr;
    if (FAILED(CoCreateInstance(
            CLSID_WbemClassObject,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_IWbemClassObject,
            reinterpret_cast<void **>(&raw)))) {
        return {};
    }
    WmiObject job(raw);
    _variant_t name(L"ReviewStorageJob");
    raw->Put(L"__CLASS", 0, &name, 0);
    _variant_t value;
    value.vt = VT_NULL;
    if (state) {
        value = _variant_t(static_cast<long>(*state));
    }
    if (FAILED(raw->Put(L"JobState", 0, &value, CIM_UINT32))) {
        return {};
    }
    value.Clear();
    value.vt = VT_NULL;
    if (errorCode) {
        value = _variant_t(static_cast<long>(*errorCode));
    }
    if (FAILED(raw->Put(L"ErrorCode", 0, &value, CIM_UINT32))) {
        return {};
    }
    return job;
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

    void refusesOversizedMbrBeforeAnyBackendStep()
    {
        struct Reporter : RestoreReporter {
            int steps = 0;
            void step(const QString &) override { ++steps; }
            void detail(const QString &) override {}
            bool cancelRequested() const override { return false; }
        } reporter;
        WindowsDiskService service;
        RestoreRequest request;
        request.disk.deviceId = QStringLiteral("synthetic-review-device");
        request.disk.size = 4ull * 1024 * 1024 * 1024 * 1024;
        request.disk.busType = UsbBusType;
        request.disk.sectorSize = 512;
        request.style = PartitionStyle::Mbr;
        request.fileSystem = FileSystemType::ExFat;
        QString error;
        QVERIFY(!service.restore(request, reporter, nullptr, &error));
        QCOMPARE(reporter.steps, 0);
        QVERIFY(error.contains(QStringLiteral("MBR")));
    }

    void explainsDeviceFailuresInPlainLanguage()
    {
        const QString context = QStringLiteral("Could not clear old signatures on disk 7");
        for (const DWORD code : {ERROR_SHARING_VIOLATION, ERROR_LOCK_VIOLATION, ERROR_BUSY}) {
            const QString text = windowsDeviceErrorText(context, code);
            QVERIFY(text.contains(QStringLiteral("another program")));
            QVERIFY(text.contains(QStringLiteral("Close any files")));
        }
        for (const DWORD code : {ERROR_ACCESS_DENIED, ERROR_WRITE_PROTECT}) {
            const QString text = windowsDeviceErrorText(context, code);
            QVERIFY(text.contains(QStringLiteral("write-protect switch")));
            QVERIFY(text.contains(QStringLiteral("read-only")));
        }
        // Write protection used to share access denied's "Windows refused access" and lose its plain cause.
        QVERIFY(windowsDeviceErrorText(context, ERROR_WRITE_PROTECT).contains(QStringLiteral("is write-protected")));
        QVERIFY(!windowsDeviceErrorText(context, ERROR_WRITE_PROTECT).contains(QStringLiteral("refused access")));
        QVERIFY(windowsDeviceErrorText(context, ERROR_ACCESS_DENIED).contains(QStringLiteral("refused access")));
        for (const DWORD code :
             {ERROR_DEV_NOT_EXIST,
              ERROR_DEVICE_NOT_CONNECTED,
              ERROR_NO_MEDIA_IN_DRIVE,
              ERROR_FILE_NOT_FOUND,
              ERROR_PATH_NOT_FOUND}) {
            const QString text = windowsDeviceErrorText(context, code);
            QVERIFY(text.contains(QStringLiteral("no longer available")));
            QVERIFY(text.contains(QStringLiteral("refresh the list and select it again")));
        }
        for (const DWORD code : {ERROR_IO_DEVICE, ERROR_CRC, ERROR_READ_FAULT, ERROR_WRITE_FAULT}) {
            const QString text = windowsDeviceErrorText(context, code);
            QVERIFY(text.contains(QStringLiteral("could not complete the read or write")));
            QVERIFY(text.contains(QStringLiteral("another USB port")));
        }
        for (const DWORD code : {ERROR_DISK_FULL, ERROR_HANDLE_DISK_FULL}) {
            QVERIFY(windowsDeviceErrorText(context, code).contains(QStringLiteral("smaller than it claims")));
        }
        const QString notReady = windowsDeviceErrorText(context, ERROR_NOT_READY);
        QVERIFY(notReady.contains(QStringLiteral("not ready")));
        QVERIFY(notReady.contains(QStringLiteral("plug it back in")));
        QVERIFY(!notReady.contains(QStringLiteral("damaged")));

        const QString other = windowsDeviceErrorText(context, ERROR_INVALID_DATA);
        QVERIFY(other.contains(windowsErrorMessage(ERROR_INVALID_DATA)));
        QVERIFY(other.contains(QStringLiteral("plug it back in")));

        for (const DWORD code :
             {ERROR_SHARING_VIOLATION,
              ERROR_LOCK_VIOLATION,
              ERROR_BUSY,
              ERROR_ACCESS_DENIED,
              ERROR_WRITE_PROTECT,
              ERROR_IO_DEVICE,
              ERROR_CRC,
              ERROR_READ_FAULT,
              ERROR_WRITE_FAULT,
              ERROR_NOT_READY,
              ERROR_DEV_NOT_EXIST,
              ERROR_DEVICE_NOT_CONNECTED,
              ERROR_NO_MEDIA_IN_DRIVE,
              ERROR_FILE_NOT_FOUND,
              ERROR_PATH_NOT_FOUND,
              ERROR_DISK_FULL,
              ERROR_HANDLE_DISK_FULL}) {
            const QString text = windowsDeviceErrorText(context, code);
            QVERIFY2(text.startsWith(context), qPrintable(text));
            QVERIFY2(text.contains(QStringLiteral("(Windows error %1). ").arg(code)), qPrintable(text));
            QVERIFY2(text.endsWith(QLatin1Char('.')), qPrintable(text));
        }
    }

    void reportsStorageFailuresWithoutDiagnosingDriveDamage()
    {
        const QString context = QStringLiteral("Could not format as exFAT on disk 7");
        for (const QString &detail : {QString(), QStringLiteral("MSFT_Volume.Format failed (0x80041001).\nDetails")}) {
            const QString text = windowsRestoreErrorText(context, detail);
            QVERIFY(text.startsWith(context));
            QVERIFY(text.contains(QStringLiteral("run the restore again")));
            QVERIFY(!text.contains(QStringLiteral("damaged")));
            if (detail.isEmpty()) {
                QVERIFY(!text.contains(QStringLiteral("Details:")));
            } else {
                QVERIFY(text.endsWith(detail));
            }
        }

        // Some details are the tool's own wording, so they must not be attributed to Windows.
        const QString ownWords = QStringLiteral("Windows did not report a new volume for disk 7 within 30 seconds.");
        QVERIFY(!windowsRestoreErrorText(context, ownWords).contains(QStringLiteral("Windows reported")));

        // A step with its own remedy replaces the replug advice instead of adding to it.
        const QString advice = QStringLiteral("Eject another drive to free a drive letter");
        const QString letter = windowsRestoreErrorText(
            QStringLiteral("Could not assign a drive letter on disk 7"),
            QStringLiteral("No available drive letter was found."),
            advice);
        QVERIFY(letter.contains(advice));
        QVERIFY(!letter.contains(QStringLiteral("Close any programs using the drive")));
    }

    void identifiesDiskAndOperationWhenRawHandleIsInvalid()
    {
        RawDisk disk(7);
        QString error;
        QVERIFY(!disk.refreshLayout(&error));
        QVERIFY(error.startsWith(QStringLiteral("Could not refresh the disk properties on disk 7")));
        QVERIFY(error.contains(windowsErrorMessage(ERROR_INVALID_HANDLE)));
        QVERIFY(error.contains(QStringLiteral("plug it back in")));

        QVERIFY(!disk.verifyIdentity(DiskInfo(), &error));
        QVERIFY(error.contains(QStringLiteral("verify disk 7")));
        QVERIFY(error.contains(QStringLiteral("Run the restore again")));
    }

    void doesNotRetryPermanentPhysicalDriveLockErrors()
    {
        RawDisk disk(7);
        QString error;
        QElapsedTimer timer;
        timer.start();
        QVERIFY(!disk.lock(&error));
        // An invalid handle used to consume all ten 500 ms waits, like an unsupported control.
        QVERIFY2(timer.elapsed() < 1000, "A permanent lock error must return without the retry delay.");
        QVERIFY(error.contains(windowsErrorMessage(ERROR_INVALID_HANDLE)));
        QVERIFY(error.contains(QStringLiteral("disk 7")));
    }

    void refusesQueuedOperationWithoutJobPath()
    {
        bool read = false;
        QString error;
        QVERIFY(!waitForStorageJob(
            QString(),
            [&](QString *) {
                read = true;
                return storageJob(7, 0);
            },
            1000,
            &error));
        QVERIFY(!read);
        QVERIFY(error.contains(QStringLiteral("without a job")));
    }

    void refusesUnreadableStorageJobs()
    {
        for (const QString &failure :
             {QStringLiteral("Access denied (0x80070005)"),
              QStringLiteral("Provider unavailable"),
              QStringLiteral("Object not found")}) {
            QString error;
            QVERIFY(!waitForStorageJob(
                QStringLiteral("job"),
                [&failure](QString *readError) {
                    *readError = failure;
                    return WmiObject();
                },
                1000,
                &error));
            QVERIFY(error.contains(failure));
        }
    }

    void requiresExplicitSuccessfulJobCompletion()
    {
        QString error;
        const auto completed = storageJob(7, 0);
        QVERIFY(completed.isValid());
        QVERIFY(waitForStorageJob(QStringLiteral("job"), [&completed](QString *) { return completed; }, 1000, &error));
        for (const auto &job :
             {storageJob(std::nullopt, 0),
              storageJob(7, std::nullopt),
              storageJob(7, 5),
              storageJob(8, 0),
              storageJob(9, 0),
              storageJob(10, 0)}) {
            QVERIFY(job.isValid());
            error.clear();
            QVERIFY(!waitForStorageJob(QStringLiteral("job"), [&job](QString *) { return job; }, 1000, &error));
            QVERIFY(!error.isEmpty());
        }
        const auto running = storageJob(4, std::nullopt);
        QVERIFY(running.isValid());
        int reads = 0;
        QVERIFY(waitForStorageJob(
            QStringLiteral("job"), [&](QString *) { return ++reads == 1 ? running : completed; }, 1000, &error));
        QCOMPARE(reads, 2);
        QVERIFY(!waitForStorageJob(QStringLiteral("job"), [&](QString *) { return running; }, 0, &error));
        QVERIFY(error.contains(QStringLiteral("did not finish")));
    }

    // The log must tell a vanished job and a missing ErrorCode apart from a clean completion.
    void describesStorageJobOutcomes()
    {
        const auto completed = storageJob(7, 0);
        const auto running = storageJob(4, std::nullopt);
        QString outcome;
        int reads = 0;
        QVERIFY(waitForStorageJob(
            QStringLiteral("job"),
            [&](QString *) { return ++reads == 1 ? running : completed; },
            1000,
            nullptr,
            &outcome));
        QVERIFY2(outcome.contains(QStringLiteral("finished with ErrorCode 0")), qPrintable(outcome));
        QVERIFY2(outcome.contains(QStringLiteral("2 read(s), last state 7")), qPrintable(outcome));

        const auto noCode = storageJob(7, std::nullopt);
        QVERIFY(!waitForStorageJob(QStringLiteral("job"), [&](QString *) { return noCode; }, 1000, nullptr, &outcome));
        QVERIFY2(outcome.contains(QStringLiteral("ErrorCode missing")), qPrintable(outcome));

        reads = 0;
        QVERIFY(!waitForStorageJob(
            QStringLiteral("job"),
            [&](QString *readError) {
                if (++reads == 1) {
                    return running;
                }
                *readError = QStringLiteral("Object not found");
                return WmiObject();
            },
            1000,
            nullptr,
            &outcome));
        QVERIFY2(outcome.contains(QStringLiteral("could not be read (Object not found)")), qPrintable(outcome));
        QVERIFY2(outcome.contains(QStringLiteral("last state 4")), qPrintable(outcome));

        QVERIFY(!waitForStorageJob(QStringLiteral("job"), [&](QString *) { return running; }, 0, nullptr, &outcome));
        QVERIFY2(outcome.contains(QStringLiteral("still running")), qPrintable(outcome));
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

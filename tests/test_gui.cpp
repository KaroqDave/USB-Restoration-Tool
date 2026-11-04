#include "gui/main_window.h"
#include "core/safety.h"
#include "core/restore_protocol.h"
#include "gui/restore_dialog.h"
#include "gui/theme.h"
#include "platform/translations.h"
#include "platform/restore_worker.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QDialogButtonBox>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLayout>
#include <QLibraryInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScopeGuard>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTextEdit>
#include <QTimer>

#include <atomic>
#include <cstdio>
#include <functional>

// Dropping the embedded catalog is how a broken package fails to load German.
extern int qInitResources_usb_gui_tests_translations();
extern int qCleanupResources_usb_gui_tests_translations();

using namespace usbrestore;

namespace {

DiskInfo exampleDisk()
{
    DiskInfo disk;
    disk.deviceId = QStringLiteral("/dev/example");
    disk.displayId = QStringLiteral("USB drive 2");
    disk.name = QStringLiteral("Example Flash Drive");
    disk.busType = UsbBusType;
    disk.size = 32ULL * 1024 * 1024 * 1024;
    return disk;
}

// The README previews show what an ISO writer leaves behind, so the erase summary has something to name.
DiskInfo installerDisk()
{
    DiskInfo disk = exampleDisk();
    disk.mountPoints = {QStringLiteral("/run/media/user/Ubuntu 24.04 LTS amd64")};
    disk.labels = {QStringLiteral("Ubuntu 24.04 LTS amd64")};
    disk.fileSystems = {QStringLiteral("iso9660")};
    return disk;
}

RestoreRequest exampleRequest()
{
    RestoreRequest request;
    request.disk = exampleDisk();
    request.volumeLabel = QStringLiteral("USB");
    return request;
}

void settleLayout(QWidget &widget)
{
    widget.ensurePolished();
    widget.resize(widget.sizeHint().expandedTo(widget.minimumSize()).expandedTo(widget.size()));
    if (QLayout *layout = widget.layout()) {
        layout->activate();
    }
    QCoreApplication::processEvents();
}

// Every widget the tests reach is addressed by its object name, so these keep the
// lookups readable and fail loudly when a name stops existing.
template <typename T> T *find(QWidget *root, const char *name)
{
    T *widget = root->findChild<T *>(QLatin1String(name));
    Q_ASSERT_X(widget, "find", name);
    return widget;
}

template <typename T> T *find(QWidget &root, const char *name)
{
    return find<T>(&root, name);
}

template <typename T> QString textOf(QWidget *root, const char *name)
{
    return find<T>(root, name)->text();
}

template <typename T> QString textOf(QWidget &root, const char *name)
{
    return textOf<T>(&root, name);
}

void confirmErase(RestoreDialog &dialog)
{
    find<QCheckBox>(dialog, "eraseAcknowledgement")->setChecked(true);
    find<QPushButton>(dialog, "confirmErase")->click();
}

QFrame *stepRow(RestoreDialog &dialog, int step)
{
    for (auto *row : dialog.findChildren<QFrame *>(QStringLiteral("restoreStepRow"))) {
        if (row->property("step").toInt() == step) {
            return row;
        }
    }
    return nullptr;
}

class ExampleService : public DiskService {
  public:
    std::atomic_int restores{0};
    int outcome = 0;
    RestoreRequest received;
    DiskInfo disk = exampleDisk();
    QVector<DiskInfo> extraDisks;
    mutable int formatChecks = 0;
    mutable int scans = 0;
    bool diskPresent = true;
    RestoreMessage scanError;
    bool reportMessages = false;
    bool holdRestore = false;
    std::atomic_bool releaseHeldRestore{false};
    RestoreMessage backendError;
    QVector<RestoreMessage> backendSteps;
    QStringList backendDetails;

    PrivilegeMode privilegeMode() const override { return PrivilegeMode::Helper; }
    QString privilegeHint() const override { return {}; }
    QVector<DiskInfo> listUsbDisks(RestoreMessage *error) const override
    {
        ++scans;
        *error = scanError;
        return diskPresent && scanError.english.isEmpty() ? QVector<DiskInfo>{disk} + extraDisks : QVector<DiskInfo>{};
    }
    bool refreshDisk(const DiskInfo &, DiskInfo *refreshedDisk, QString *, QString * = nullptr) const override
    {
        *refreshedDisk = disk;
        return true;
    }
    RestoreGuard restoreGuard() const override { return {}; }
    QVector<FileSystemType> supportedFileSystems() const override { return {FileSystemType::ExFat}; }
    bool canFormatFileSystem(FileSystemType, const DiskInfo &, QString *, QString * = nullptr) const override
    {
        ++formatChecks;
        return true;
    }
    const QVector<RestoreStep> &restorePlan() const override { return linuxRestorePlan(); }
    int totalRestoreSteps() const override { return static_cast<int>(restorePlan().size()); }
    int firstDestructiveStep() const override { return firstDestructiveStepOf(restorePlan()); }
    bool restore(
        const RestoreRequest &request,
        RestoreReporter &reporter,
        RestoreResult *result,
        QString *error,
        QString *displayError = nullptr) override
    {
        received = request;
        ++restores;
        if (holdRestore) {
            QElapsedTimer timeout;
            timeout.start();
            while (!releaseHeldRestore.load() && !reporter.cancelRequested() && timeout.elapsed() < 15000) {
                QThread::msleep(1);
            }
            if (reporter.cancelRequested()) {
                return false;
            }
            if (!releaseHeldRestore.load()) {
                *error = QStringLiteral("Example hold timed out");
                return false;
            }
            if (result) {
                result->location = QStringLiteral("/dev/example1");
            }
            return true;
        }
        if (reportMessages) {
            for (const auto &message : backendSteps) {
                reporter.step(message);
            }
            for (const auto &detail : backendDetails) {
                reporter.detail(detail);
            }
            setRestoreError(error, displayError, backendError);
            return false;
        }
        if (outcome == 1) {
            *error = QStringLiteral("Example restore failure");
            return false;
        }
        if (outcome == 2) {
            reporter.step(QStringLiteral("Checking the example drive"));
            QElapsedTimer timeout;
            timeout.start();
            while (!reporter.cancelRequested() && timeout.elapsed() < 3000) {
                QThread::msleep(1);
            }
            if (!reporter.cancelRequested()) {
                *error = QStringLiteral("Example cancellation timed out");
            }
            return false;
        }
        if (outcome == 3) {
            for (int index = 0; index < firstDestructiveStep(); ++index) {
                reporter.step(QStringLiteral("Example step %1").arg(index + 1));
            }
            QElapsedTimer timeout;
            timeout.start();
            while (!reporter.cancelRequested() && timeout.elapsed() < 2000) {
                QThread::msleep(1);
            }
            if (reporter.cancelRequested()) {
                *error = QStringLiteral("Example cancel was accepted after writing started");
            } else {
                *error = QStringLiteral("Example stopped after writing started");
            }
            return false;
        }
        for (int index = 0; index < totalRestoreSteps(); ++index) {
            reporter.step(QStringLiteral("Example step %1").arg(index + 1));
        }
        result->location = QStringLiteral("/dev/example1");
        return true;
    }
};

} // namespace

class GuiTests : public QObject {
    Q_OBJECT
  private slots:
    void initTestCase()
    {
        // ctest captures a pipe. The Windows CRT buffers that fully, so a crash
        // drops the assertion text.
        setvbuf(stdout, nullptr, _IONBF, 0);
        setvbuf(stderr, nullptr, _IONBF, 0);
        QLocale::setDefault(QLocale::c());
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }

    void remembersTheLanguagePreference()
    {
        const AppSettings original = loadAppSettings();
        const auto restore = qScopeGuard([original]() { saveAppSettings(original); });
        QSettings store(QStringLiteral("KaroqDave"), QStringLiteral("USB Restoration Tool"));
        store.remove(QStringLiteral("language"));
        QCOMPARE(loadAppSettings().language, Language::System);
        for (const auto language : {Language::System, Language::English, Language::German}) {
            AppSettings settings = original;
            settings.language = language;
            saveAppSettings(settings);
            const AppSettings loaded = loadAppSettings();
            QCOMPARE(loaded.language, language);
            QCOMPARE(loaded.theme, original.theme);
            QCOMPARE(loaded.partitionStyle, original.partitionStyle);
            QCOMPARE(loaded.fileSystem, original.fileSystem);
        }
        store.setValue(QStringLiteral("language"), QStringLiteral("unsupported"));
        QCOMPARE(loadAppSettings().language, Language::System);
    }

    void loadsOnlyEmbeddedTranslationsWithoutChangingTheLocale()
    {
        const QLocale locale;
        const QString englishTitle = RestoreDialog::tr("Confirm USB restore");
        {
            Translations translations;
            QVERIFY(translations.load(Language::German));
            QCOMPARE(translations.language(), Language::German);
            QCOMPARE(RestoreDialog::tr("Confirm USB restore"), QStringLiteral("USB-Wiederherstellung bestätigen"));
            QDialogButtonBox buttons(QDialogButtonBox::Cancel);
            QCOMPARE(buttons.button(QDialogButtonBox::Cancel)->text(), QStringLiteral("Abbrechen"));
            QCOMPARE(QLocale(), locale);
            QVERIFY(translations.load(Language::English));
            QCOMPARE(translations.language(), Language::English);
            QCOMPARE(RestoreDialog::tr("Confirm USB restore"), englishTitle);
            QVERIFY(translations.load(Language::German));
        }
        QCOMPARE(RestoreDialog::tr("Confirm USB restore"), englishTitle);
        QCOMPARE(QLocale(), locale);
    }

    void activatesTheSavedLanguageWithoutChangingTheLocale()
    {
        const QLocale locale;
        const AppSettings original = loadAppSettings();
        const auto restoreSettings = qScopeGuard([original]() { saveAppSettings(original); });
        AppSettings settings = original;
        settings.language = Language::German;
        saveAppSettings(settings);
        {
            Translations translations;
            QVERIFY(activateInterfaceLanguage(translations, loadAppSettings().language));
            QCOMPARE(translations.language(), Language::German);
            QCOMPARE(elevatedPermissionTitle(), QStringLiteral("Erhöhte Berechtigung erforderlich"));
            QCOMPARE(
                QCoreApplication::translate("usbrestore::Language", "Language: German"),
                QStringLiteral("Sprache: Deutsch"));
        }
        settings.language = Language::English;
        saveAppSettings(settings);
        {
            Translations translations;
            QVERIFY(activateInterfaceLanguage(translations, loadAppSettings().language));
            QCOMPARE(translations.language(), Language::English);
            QCOMPARE(elevatedPermissionTitle(), QStringLiteral("Elevated permission required"));
            QCOMPARE(
                QCoreApplication::translate("usbrestore::Language", "Language: English"),
                QStringLiteral("Language: English"));
        }
        {
            Translations translations;
            QVERIFY(translations.load(Language::German));
            const auto restoreCatalog = qScopeGuard([]() { qInitResources_usb_gui_tests_translations(); });
            qCleanupResources_usb_gui_tests_translations();
            QVERIFY(!QFile::exists(QStringLiteral(":/i18n/usbrestore_de.qm")));
            QVERIFY(!activateInterfaceLanguage(translations, Language::German));
            QCOMPARE(translations.language(), Language::English);
            QCOMPARE(elevatedPermissionTitle(), QStringLiteral("Elevated permission required"));
        }
        QCOMPARE(QLocale(), locale);
    }

    void switchesLanguageFromTheHeader()
    {
        const AppSettings original = loadAppSettings();
        const auto restoreSettings = qScopeGuard([original]() { saveAppSettings(original); });
        AppSettings settings = original;
        settings.language = Language::English;
        saveAppSettings(settings);
        Translations translations;
        QVERIFY(translations.load(Language::English));
        ExampleService service;
        MainWindow window(service, Theme::Light, &translations);
        window.show();
        auto *language = find<QPushButton>(window, "language");
        QVERIFY(language);
        QCOMPARE(language->text(), QStringLiteral("Language: English"));
        QCOMPARE(textOf<QLabel>(window, "subtitle"), QStringLiteral("A fresh start for your USB drive."));

        language->click();
        QCoreApplication::processEvents();
        QCOMPARE(loadAppSettings().language, Language::German);
        QCOMPARE(translations.language(), Language::German);
        QCOMPARE(language->text(), QStringLiteral("Sprache: Deutsch"));
        QCOMPARE(textOf<QLabel>(window, "subtitle"), QStringLiteral("Ein Neuanfang für Ihr USB-Laufwerk."));
        QCOMPARE(textOf<QPushButton>(window, "restoreUsb"), QStringLiteral("USB wiederherstellen"));

        language->click();
        QCoreApplication::processEvents();
        QCOMPARE(loadAppSettings().language, Language::System);
        QCOMPARE(translations.language(), resolveLanguage(Language::System));
        QCOMPARE(language->text(), languageButtonText(Language::System));

        language->click();
        QCoreApplication::processEvents();
        QCOMPARE(loadAppSettings().language, Language::English);
        QCOMPARE(translations.language(), Language::English);
        QCOMPARE(textOf<QLabel>(window, "subtitle"), QStringLiteral("A fresh start for your USB drive."));
        window.close();
    }

    void keepsTheHeaderReachableAfterChangingLanguage_data()
    {
        QTest::addColumn<Language>("start");
        QTest::newRow("from-english") << Language::English;
        QTest::newRow("from-german") << Language::German;
    }

    // Which language needs more room depends on the font, and with horizontal scrolling
    // off a window shrunk to the narrower one would cut off the header in the wider one.
    void keepsTheHeaderReachableAfterChangingLanguage()
    {
        QFETCH(Language, start);
        const AppSettings original = loadAppSettings();
        const auto restoreSettings = qScopeGuard([original]() { saveAppSettings(original); });
        AppSettings settings = original;
        settings.language = start;
        settings.geometry.clear();
        saveAppSettings(settings);
        Translations translations;
        QVERIFY(translations.load(start));
        ExampleService service;
        MainWindow window(service, Theme::Light, &translations);
        window.resize(window.minimumWidth(), window.height());
        window.show();
        QCoreApplication::processEvents();
        const int startingMinimum = window.minimumWidth();

        auto *language = find<QPushButton>(window, "language");
        auto *scroll = qobject_cast<QScrollArea *>(window.centralWidget());
        QVERIFY(language && scroll);
        for (int click = 0; click < 3; ++click) {
            language->click();
            QCoreApplication::processEvents();
            QCoreApplication::sendPostedEvents(nullptr, QEvent::LayoutRequest);
            QCoreApplication::processEvents();
            QVERIFY2(
                scroll->viewport()->width() >= scroll->widget()->minimumSizeHint().width(),
                qPrintable(QStringLiteral("viewport %1, page needs %2")
                               .arg(scroll->viewport()->width())
                               .arg(scroll->widget()->minimumSizeHint().width())));
            QVERIFY2(language->minimumWidth() >= language->sizeHint().width(), qPrintable(language->text()));
        }
        QCOMPARE(translations.language(), start);
        QCOMPARE(window.minimumWidth(), startingMinimum);
        window.close();
    }

    void reactsOnceToTheSelectedDiskWhenTheLanguageChanges()
    {
        const AppSettings original = loadAppSettings();
        const auto restoreSettings = qScopeGuard([original]() { saveAppSettings(original); });
        AppSettings settings = original;
        settings.language = Language::English;
        saveAppSettings(settings);
        Translations translations;
        QVERIFY(translations.load(Language::English));
        ExampleService service;
        DiskInfo second = service.disk;
        second.deviceId = QStringLiteral("/dev/example-second");
        second.displayId = QStringLiteral("example-second");
        service.extraDisks = {second};
        MainWindow window(service, Theme::Light, &translations);
        window.show();
        auto *disks = window.findChild<QListWidget *>();
        auto *language = find<QPushButton>(window, "language");
        QVERIFY(disks && language);
        QCOMPARE(disks->count(), 2);
        disks->setCurrentRow(1);

        QSignalSpy selection(disks, &QListWidget::currentRowChanged);
        service.formatChecks = 0;
        language->click();
        QCoreApplication::processEvents();
        QCOMPARE(translations.language(), Language::German);
        QCOMPARE(disks->currentRow(), 1);
        QCOMPARE(service.formatChecks, 1);
        QCOMPARE(selection.count(), 0);
        window.close();
    }

    void doesNotChangeLanguageDuringARestore()
    {
        const AppSettings original = loadAppSettings();
        const auto restoreSettings = qScopeGuard([original]() { saveAppSettings(original); });
        AppSettings settings = original;
        settings.language = Language::English;
        saveAppSettings(settings);
        Translations translations;
        QVERIFY(translations.load(Language::English));
        ExampleService service;
        service.holdRestore = true;
        MainWindow window(service, Theme::Light, &translations);
        window.show();
        auto *language = find<QPushButton>(window, "language");
        QVERIFY(language);
        QCOMPARE(language->text(), QStringLiteral("Language: English"));
        find<QPushButton>(window, "restoreUsb")->click();
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        confirmErase(*dialog);
        QVERIFY(!language->isEnabled());
        language->click();
        QVERIFY(QMetaObject::invokeMethod(&window, "cycleLanguage"));
        QCOMPARE(language->text(), QStringLiteral("Language: English"));
        QCOMPARE(textOf<QLabel>(window, "subtitle"), QStringLiteral("A fresh start for your USB drive."));
        QCOMPARE(loadAppSettings().language, Language::English);
        const QString log = find<QTextEdit>(window, "activityLog")->toPlainText();
        QVERIFY(log.contains(QStringLiteral("Restore requested for")));
        QVERIFY(log.contains(QStringLiteral("32.00 GB")));
        QVERIFY(!log.contains(QStringLiteral("32,00 GB")));
        service.releaseHeldRestore.store(true);
        QTRY_COMPARE(dialog->windowTitle(), QStringLiteral("Restore complete"));
        QCOMPARE(
            dialog->findChild<QLabel *>(QStringLiteral("restoreMessage"))->text(),
            QStringLiteral("The disk is now one exFAT volume labelled USB at /dev/example1."));
        QVERIFY(language->isEnabled());
        QCOMPARE(translations.language(), Language::English);
        dialog->accept();
        window.close();
    }

    void retranslatesScanFailuresWithoutRescanning_data()
    {
        QTest::addColumn<bool>("diskPresentAfterRefresh");
        QTest::newRow("disk-found") << true;
        QTest::newRow("no-disks") << false;
    }

    void retranslatesScanFailuresWithoutRescanning()
    {
        QFETCH(bool, diskPresentAfterRefresh);
        const AppSettings original = loadAppSettings();
        const auto restoreSettings = qScopeGuard([original]() { saveAppSettings(original); });
        AppSettings settings = original;
        settings.language = Language::English;
        saveAppSettings(settings);
        Translations translations;
        QVERIFY(translations.load(Language::English));
        ExampleService service;
        // Composed like the backends do: application wording around an OS diagnostic.
        const RestoreMessage failure =
            RestoreMessage(QStringLiteral("%1 (%2)"))
                .arg(
                    restoreText(QT_TRANSLATE_NOOP(
                        "usbrestore::Backend", "/sys/block is not available, so block devices cannot be listed.")),
                    QStringLiteral("errno 5"));
        service.scanError = failure;
        MainWindow window(service, Theme::Light, &translations);
        window.show();
        auto *status = find<QLabel>(window, "statusBadge");
        auto *restore = find<QPushButton>(window, "restoreUsb");
        auto *language = find<QPushButton>(window, "language");
        auto *log = find<QTextEdit>(window, "activityLog");
        QVERIFY(status);
        QVERIFY(restore);
        QVERIFY(language);
        QVERIFY(log);
        QVERIFY(status->text().contains(QStringLiteral("Could not scan for USB disks.")));
        QLabel *detail = nullptr;
        for (auto *label : window.findChildren<QLabel *>()) {
            if (label->text() == failure.english) {
                detail = label;
            }
        }
        QVERIFY(detail);
        const QString originalLog = log->toPlainText();
        QVERIFY(originalLog.contains(failure.english));
        const int scans = service.scans;

        service.scanError = {};
        for (int change = 0; change < 3; ++change) {
            language->click();
            QCoreApplication::processEvents();
            QVERIFY(status->text().contains(
                QCoreApplication::translate("usbrestore::MainWindow", "Could not scan for USB disks.")));
            QCOMPARE(detail->text(), failure.translated());
            if (translations.language() == Language::German) {
                QVERIFY(detail->text() != failure.english);
                QVERIFY(detail->text().contains(QStringLiteral("errno 5")));
            }
            QVERIFY(!restore->isEnabled());
            QCOMPARE(log->toPlainText(), originalLog);
        }
        QCOMPARE(translations.language(), Language::English);
        QCOMPARE(detail->text(), failure.english);
        QCOMPARE(service.scans, scans);
        QCOMPARE(service.restores.load(), 0);

        service.diskPresent = diskPresentAfterRefresh;
        QVERIFY(QMetaObject::invokeMethod(&window, "refreshDisks"));
        QVERIFY(!status->text().contains(QStringLiteral("Could not scan for USB disks.")));
        QVERIFY(detail->text() != failure.english);
        QCOMPARE(restore->isEnabled(), diskPresentAfterRefresh);
        if (diskPresentAfterRefresh) {
            QVERIFY(status->text().contains(QStringLiteral("Ready to restore")));
        } else {
            QVERIFY(status->text().contains(QStringLiteral("Select a USB disk")));
        }
        window.close();
    }

    void translatesSharedDescriptionsWithoutChangingLogs()
    {
        Translations translations;
        QVERIFY(translations.load(Language::German));
        const QLocale locale;
        DiskInfo disk = exampleDisk();
        disk.name.clear();
        QCOMPARE(formatByteSize(disk.size), QStringLiteral("32.00 GB"));
        QCOMPARE(formatByteSize(disk.size, TextMode::Display), QStringLiteral("32,00 GB"));
        QCOMPARE(formatByteSize(512, TextMode::Display), QStringLiteral("512 B"));
        QCOMPARE(formatDecimal(1234.5, 1), QStringLiteral("1234.5"));
        QCOMPARE(formatDecimal(1234.5, 1, TextMode::Display), QStringLiteral("1234,5"));
        QCOMPARE(describeDisk(disk), QStringLiteral("USB drive 2 — USB disk (32.00 GB)"));
        QCOMPARE(describeDisk(disk, TextMode::Display), QStringLiteral("USB drive 2 — USB-Laufwerk (32,00 GB)"));
        QCOMPARE(partitionStyleName(0), QStringLiteral("RAW / unknown"));
        QCOMPARE(partitionStyleName(0, TextMode::Display), QStringLiteral("RAW / unbekannt"));
        disk.size = LargeRestoreTargetBytes;
        QVERIFY(largeRestoreTargetWarning(disk).startsWith(QStringLiteral("Large USB disk: 128.00 GB.")));
        QVERIFY(largeRestoreTargetWarning(disk, TextMode::Display)
                    .startsWith(QStringLiteral("Großes USB-Laufwerk: 128,00 GB.")));
        QString english;
        QString german;
        disk.isReadOnly = true;
        QVERIFY(!isSafeRestoreTarget(disk, {}, &english));
        QVERIFY(!isSafeRestoreTarget(disk, {}, &german, TextMode::Display));
        QCOMPARE(english, QStringLiteral("USB drive 2 is read-only."));
        QCOMPARE(german, QStringLiteral("USB drive 2 ist schreibgeschützt."));
        disk.size = 3ull * 1024 * 1024 * 1024 * 1024;
        QVERIFY(!canCreatePartitionLayout(PartitionStyle::Mbr, disk, &english));
        QVERIFY(!canCreatePartitionLayout(PartitionStyle::Mbr, disk, &german, TextMode::Display));
        QVERIFY(english.startsWith(QStringLiteral("MBR cannot address")));
        QVERIFY(german.startsWith(QStringLiteral("MBR kann eine Partition")));
        DiskInfo changed = disk;
        changed.serialNumber = QStringLiteral("new");
        disk.serialNumber = QStringLiteral("old");
        QVERIFY(!isSameRestoreTarget(disk, changed, &english));
        QVERIFY(!isSameRestoreTarget(disk, changed, &german, TextMode::Display));
        QVERIFY(english.contains(QStringLiteral("same serial number")));
        QVERIFY(german.contains(QStringLiteral("„Seriennummer“")));
        QVERIFY(!isSameDeviceSequence(disk.deviceId, 1, 2, &german, TextMode::Display));
        QVERIFY(german.startsWith(QStringLiteral("Es konnte nicht bestätigt werden")));
        QCOMPARE(QLocale(), locale);
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        QVERIFY(dialog.setProgress(1, static_cast<int>(linuxRestorePlan().size()), QStringLiteral("Prüfen")));
        QVERIFY(dialog.setProgress(2, static_cast<int>(linuxRestorePlan().size()), QStringLiteral("Erkennen")));
        const QString duration = stepRow(dialog, 1)->findChild<QLabel *>(QStringLiteral("restoreStepState"))->text();
        QVERIFY(duration.contains(QLatin1Char(',')));
        QVERIFY(!duration.contains(QLatin1Char('.')));
        dialog.finishCancelled(QStringLiteral("Test beendet"));
        QVERIFY(translations.load(Language::English));
        QCOMPARE(formatByteSize(disk.size, TextMode::Display), formatByteSize(disk.size));
    }

    void showsTranslatedSafetyRefusalsWithoutStartingARestore_data()
    {
        QTest::addColumn<int>("scenario");
        QTest::addColumn<bool>("dark");
        for (const bool dark : {false, true}) {
            for (int scenario = 0; scenario < 4; ++scenario) {
                const QByteArray name = QByteArray(dark ? "dark-" : "light-") + QByteArray::number(scenario);
                QTest::newRow(name.constData()) << scenario << dark;
            }
        }
    }

    void showsTranslatedSafetyRefusalsWithoutStartingARestore()
    {
        QFETCH(int, scenario);
        QFETCH(bool, dark);
        Translations translations;
        QVERIFY(translations.load(Language::German));
        QSettings(QStringLiteral("KaroqDave"), QStringLiteral("USB Restoration Tool")).clear();
        ExampleService service;
        if (scenario == 0)
            service.disk.isReadOnly = true;
        if (scenario == 1)
            service.disk.busType = 0;
        if (scenario == 2)
            service.disk.size = 3ull * 1024 * 1024 * 1024 * 1024;
        if (scenario == 3)
            service.disk.size = LargeRestoreTargetBytes;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        if (scenario == 2) {
            for (auto *combo : window.findChildren<QComboBox *>()) {
                if (combo->accessibleName() == QStringLiteral("Partitionsschema"))
                    combo->setCurrentIndex(1);
            }
        }
        auto *restore = find<QPushButton>(window, "restoreUsb");
        QVERIFY(restore);
        QCOMPARE(restore->isEnabled(), scenario == 3);
        QString details;
        for (auto *label : window.findChildren<QLabel *>())
            details += label->text();
        const QString expected[] = {
            QStringLiteral("ist schreibgeschützt"),
            QStringLiteral("Bus Unbekannt"),
            QStringLiteral("MBR kann eine Partition"),
            QStringLiteral("Großes USB-Laufwerk: 128,00 GB")};
        QVERIFY2(details.contains(expected[scenario]), qPrintable(details));
        if (scenario != 3) {
            restore->click();
            QVERIFY(!window.findChild<RestoreDialog *>());
        } else {
            restore->click();
            auto *dialog = window.findChild<RestoreDialog *>();
            QVERIFY(dialog);
            QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("confirmErase"))->isEnabled());
            dialog->reject();
        }
        QCOMPARE(service.restores.load(), 0);
        window.close();
    }

    void separatesBackendDisplayMessagesFromEnglishLogs_data()
    {
        QTest::addColumn<Language>("language");
        QTest::newRow("english") << Language::English;
        QTest::newRow("german") << Language::German;
    }

    void separatesBackendDisplayMessagesFromEnglishLogs()
    {
        QFETCH(Language, language);
        Translations translations;
        QVERIFY(translations.load(language));
        ExampleService service;
        service.reportMessages = true;
        const auto request = exampleRequest();
        service.backendSteps = {
            restoreStepMessage(RestoreStep::VerifyDisk, request.style, request.fileSystem),
            restoreStepMessage(RestoreStep::Format, request.style, request.fileSystem)};
        const QString diagnostic = QStringLiteral("formatter detail: device /dev/example1, code 73, %1");
        service.backendDetails = {diagnostic};
        service.backendError = restoreText(QT_TRANSLATE_NOOP(
                                               "usbrestore::Backend",
                                               "%1 failed to format the new volume on %2: %3\n\nUnplug the drive, plug "
                                               "it back in and run the restore again."))
                                   .arg(QStringLiteral("mkfs-test"), QStringLiteral("/dev/example1"), diagnostic);
        RestoreWorker worker(service, request);
        QSignalSpy logs(&worker, &RestoreWorker::logMessage);
        QSignalSpy details(&worker, &RestoreWorker::logFileOnly);
        QSignalSpy progress(&worker, &RestoreWorker::progress);
        QSignalSpy failed(&worker, &RestoreWorker::failed);
        worker.run();
        QCOMPARE(logs.size(), 3);
        QCOMPARE(progress.size(), 2);
        QCOMPARE(logs.at(0).first().toString(), QStringLiteral("Verifying the selected USB disk"));
        QCOMPARE(logs.at(1).first().toString(), QStringLiteral("Formatting as exFAT"));
        QCOMPARE(logs.at(2).first().toString(), QStringLiteral("ERROR: ") + service.backendError.english);
        QVERIFY(service.backendError.english.contains(diagnostic));
        QVERIFY(service.backendError.display.contains(diagnostic));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), service.backendError.display);
        bool sawDetail = false;
        for (const auto &line : details) {
            sawDetail |= line.first().toString() == diagnostic;
        }
        QVERIFY(sawDetail);
        QCOMPARE(progress.at(0).at(2).toString(), service.backendSteps.first().display);
        if (language == Language::German) {
            QCOMPARE(progress.at(1).at(2).toString(), QStringLiteral("Als exFAT formatieren"));
            QVERIFY(failed.first().first().toString().contains(QStringLiteral("konnte das neue Volume")));
        } else {
            QCOMPARE(service.backendError.display, service.backendError.english);
        }
    }

    void translatesTheFallbackFailureWithoutChangingItsLog()
    {
        Translations translations;
        QVERIFY(translations.load(Language::German));
        ExampleService service;
        service.reportMessages = true;
        RestoreWorker worker(service, exampleRequest());
        QSignalSpy logs(&worker, &RestoreWorker::logMessage);
        QSignalSpy failed(&worker, &RestoreWorker::failed);
        worker.run();
        QCOMPARE(logs.first().first().toString(), QStringLiteral("ERROR: The restore failed."));
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Die Wiederherstellung ist fehlgeschlagen."));
    }

    void translatesEveryBackendStepWithoutChangingItsLogText()
    {
        struct StepMessage {
            RestoreStep step;
            PartitionStyle style;
            FileSystemType fileSystem;
            RestoreMessage message;
        };
        const auto everyStepMessage = []() {
            QVector<StepMessage> messages;
            for (const auto &plan : {linuxRestorePlan(), windowsRestorePlan()}) {
                for (const RestoreStep step : plan) {
                    for (const auto style : {PartitionStyle::Gpt, PartitionStyle::Mbr}) {
                        for (const auto fs :
                             {FileSystemType::ExFat,
                              FileSystemType::Fat32,
                              FileSystemType::Ntfs,
                              FileSystemType::Ext4}) {
                            messages.append({step, style, fs, restoreStepMessage(step, style, fs)});
                        }
                    }
                }
            }
            return messages;
        };
        const QVector<StepMessage> before = everyStepMessage();
        Translations translations;
        QVERIFY(translations.load(Language::German));
        const QVector<StepMessage> after = everyStepMessage();
        QCOMPARE(after.size(), before.size());
        for (qsizetype index = 0; index < after.size(); ++index) {
            const StepMessage &entry = after.at(index);
            const RestoreMessage &message = entry.message;
            QVERIFY2(!message.english.isEmpty(), qPrintable(message.display));
            QCOMPARE(message.english, before.at(index).message.english);
            QVERIFY2(message.display != message.english, qPrintable(message.english));
            if (entry.step == RestoreStep::Format) {
                QVERIFY(message.display.contains(fileSystemTypeName(entry.fileSystem)));
            }
            if (entry.step == RestoreStep::CreatePartition || entry.step == RestoreStep::WritePartitionTable) {
                QVERIFY(message.display.contains(partitionStyleLabel(entry.style)));
            }
        }
    }

    void translatesHelperFailuresWithoutChangingProtocolOrDetails()
    {
        Translations translations;
        QVERIFY(translations.load(Language::German));
        HelperRunObservation run;
        run.sawVersion = true;
        run.versionAccepted = true;
        run.exitCode = RestoreExitFailed;
        run.helperStderr = QStringLiteral("Could not open /dev/example: errno 5");
        QString english, display;
        QCOMPARE(judgeHelperRun(run, &english), HelperRunVerdict::Failed);
        QCOMPARE(judgeHelperRun(run, &display, TextMode::Display), HelperRunVerdict::Failed);
        QCOMPARE(english, run.helperStderr);
        QCOMPARE(display, QStringLiteral("Die Wiederherstellung ist fehlgeschlagen.\n\nDetails: ") + run.helperStderr);
        const auto line = parseProtocolLine(
            encodeVersionLine(RestoreProtocolVersion + 1, QStringLiteral("other"), linuxRestorePlan()));
        QVERIFY(!acceptHelperAnnouncement(line, QStringLiteral("test"), linuxRestorePlan(), &english));
        QVERIFY(
            !acceptHelperAnnouncement(line, QStringLiteral("test"), linuxRestorePlan(), &display, TextMode::Display));
        QVERIFY(english.contains(QStringLiteral("partly upgraded")));
        QVERIFY(display.contains(QStringLiteral("teilweise aktualisiert")));
        QCOMPARE(
            encodeStepLine(1, 10, QStringLiteral("Verifying the selected USB disk")),
            QStringLiteral("step 1/10 Verifying the selected USB disk"));
        QVERIFY(translations.load(Language::English));
        QCOMPARE(judgeHelperRun(run, &display, TextMode::Display), HelperRunVerdict::Failed);
        QCOMPARE(display, run.helperStderr);
    }

    void keepsBackendErrorsInEnglishWhileTheDialogIsGerman()
    {
        Translations translations;
        QVERIFY(translations.load(Language::German));
        ExampleService service;
        service.outcome = 1;
        RestoreWorker worker(service, exampleRequest());
        QSignalSpy logs(&worker, &RestoreWorker::logMessage);
        QSignalSpy failed(&worker, &RestoreWorker::failed);
        worker.run();
        QCOMPARE(logs.size(), 1);
        QCOMPARE(logs.first().first().toString(), QStringLiteral("ERROR: Example restore failure"));
        QCOMPARE(failed.size(), 1);
        QCOMPARE(failed.first().first().toString(), QStringLiteral("Example restore failure"));
        QCOMPARE(RestoreDialog::tr("Restore failed"), QStringLiteral("Wiederherstellung fehlgeschlagen"));
    }

#ifdef Q_OS_LINUX
    void refusesHostQtLibrariesInABundle_data()
    {
        QTest::addColumn<QString>("hostLibrary");
        QTest::addColumn<bool>("symlink");
        QTest::newRow("complete") << QString() << false;
        QTest::newRow("missing-gui") << QStringLiteral("libQt6Gui.so.6") << false;
        QTest::newRow("missing-widgets") << QStringLiteral("libQt6Widgets.so.6") << false;
        QTest::newRow("symlink-to-host") << QStringLiteral("libQt6Gui.so.6") << true;
    }

    void refusesHostQtLibrariesInABundle()
    {
        QFETCH(QString, hostLibrary);
        QFETCH(bool, symlink);
        QTemporaryDir bundle;
        QVERIFY(bundle.isValid());
        const QString libraryDirectory = bundle.filePath(QStringLiteral("lib"));
        QVERIFY(QDir().mkdir(libraryDirectory));
        const QString iconPath = bundle.filePath(QStringLiteral("icon.png"));
        QImage icon(32, 32, QImage::Format_ARGB32);
        icon.fill(Qt::green);
        QVERIFY(icon.save(iconPath));

        const QString probe = QCoreApplication::applicationDirPath() + QStringLiteral("/usb-qt-bundle-probe");
        const QStringList arguments{QLibraryInfo::path(QLibraryInfo::PluginsPath), iconPath};
        auto environment = QProcessEnvironment::systemEnvironment();
        const QString inheritedPath = environment.value(QStringLiteral("LD_LIBRARY_PATH"));
        environment.insert(
            QStringLiteral("LD_LIBRARY_PATH"),
            QLibraryInfo::path(QLibraryInfo::LibrariesPath) +
                (inheritedPath.isEmpty() ? QString() : QLatin1Char(':') + inheritedPath));
        QProcess reference;
        reference.setProcessEnvironment(environment);
        reference.start(probe, arguments);
        QVERIFY(reference.waitForFinished(10000));
        const QByteArray referenceError = reference.readAllStandardError();
        QCOMPARE(reference.exitStatus(), QProcess::NormalExit);
        QVERIFY2(reference.exitCode() == 0, referenceError.constData());
        const QStringList lines = QString::fromLocal8Bit(reference.readAllStandardOutput()).split(QLatin1Char('\n'));
        bool foundCore = false;
        bool foundHostLibrary = hostLibrary.isEmpty();
        for (const QString &line : lines) {
            const QString prefix = QStringLiteral("Loaded Qt library: ");
            if (!line.startsWith(prefix)) {
                continue;
            }
            const QString path = line.mid(prefix.size());
            const QString name =
                QFileInfo(path).fileName().section(QStringLiteral(".so"), 0, 0) + QStringLiteral(".so.6");
            const QString destination = QDir(libraryDirectory).filePath(name);
            foundCore |= name == QStringLiteral("libQt6Core.so.6");
            if (name == hostLibrary) {
                foundHostLibrary = true;
                if (symlink) {
                    QVERIFY(QFile::link(path, destination));
                }
            } else {
                QVERIFY2(QFile::copy(path, destination), qPrintable(path));
            }
        }
        QVERIFY(foundCore);
        QVERIFY(foundHostLibrary);

        environment.insert(
            QStringLiteral("LD_LIBRARY_PATH"),
            libraryDirectory + QLatin1Char(':') + environment.value(QStringLiteral("LD_LIBRARY_PATH")));
        QProcess bundled;
        bundled.setProcessEnvironment(environment);
        bundled.start(probe, arguments + QStringList{libraryDirectory});
        QVERIFY(bundled.waitForFinished(10000));
        const QByteArray error = bundled.readAllStandardError();
        QCOMPARE(bundled.exitStatus(), QProcess::NormalExit);
        QVERIFY2(bundled.exitCode() == (hostLibrary.isEmpty() ? 0 : 1), error.constData());
        if (!hostLibrary.isEmpty()) {
            QVERIFY2(error.contains("Qt library loaded outside the bundle:"), error.constData());
            QVERIFY2(error.contains(hostLibrary.toLocal8Bit()), error.constData());
        }
    }
#endif

    void reportsStartedStepsWithoutClaimingSuccess()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        auto *progress = dialog.findChild<QProgressBar *>();
        QCOMPARE(progress->value(), 0);
        QCOMPARE(dialog.findChildren<QFrame *>(QStringLiteral("restoreStepRow")).size(), linuxRestorePlan().size());
        const int total = static_cast<int>(linuxRestorePlan().size());
        QVERIFY(!dialog.setProgress(2, total, QStringLiteral("Skipped ahead")));
        QCOMPARE(stepRow(dialog, 2)->property("state").toString(), QStringLiteral("pending"));
        for (int step = 1; step <= total; ++step) {
            QVERIFY(dialog.setProgress(step, total, QStringLiteral("Working")));
            QVERIFY(!dialog.setProgress(step, total, QStringLiteral("Repeated")));
            QCOMPARE(progress->value(), step - 1);
            QCOMPARE(stepRow(dialog, step)->property("state").toString(), QStringLiteral("active"));
            if (step > 1) {
                QCOMPARE(stepRow(dialog, step - 1)->property("state").toString(), QStringLiteral("complete"));
            }
        }
        dialog.finishSuccess(QStringLiteral("Ready at /dev/example1"));
        QCOMPARE(progress->value(), progress->maximum());
        QCOMPARE(stepRow(dialog, progress->maximum())->property("state").toString(), QStringLiteral("complete"));
        QVERIFY(!dialog.setProgress(1, progress->maximum(), QStringLiteral("Late progress")));
        QCOMPARE(progress->value(), progress->maximum());
    }

    void preventsDismissalUntilAnOutcome()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        dialog.show();
        for (int step = 1; step <= firstDestructiveStepOf(linuxRestorePlan()); ++step) {
            dialog.setProgress(step, 10, QStringLiteral("Writing"));
        }
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QVERIFY(dialog.isVisible());
        dialog.close();
        QVERIFY(dialog.isVisible());
        dialog.accept();
        QVERIFY(dialog.isVisible());
        dialog.finishFailure(QStringLiteral("Example failure"));
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QVERIFY(!dialog.isVisible());
    }

    void disablesCancellationWhenWritingStarts()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        auto *cancel = find<QPushButton>(dialog, "cancelRestore");
        QSignalSpy requested(&dialog, &RestoreDialog::cancelRequested);
        connect(&dialog, &RestoreDialog::cancelRequested, &dialog, &RestoreDialog::setCancelling);
        dialog.show();
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(requested.count(), 1);
        QVERIFY(!cancel->isEnabled());
        QVERIFY(textOf<QLabel>(dialog, "restoreNotice").contains(QStringLiteral("checkpoint")));
        // A request can arrive after the backend's last checkpoint; progress must still be shown.
        for (int step = 1; step <= firstDestructiveStepOf(linuxRestorePlan()); ++step) {
            dialog.setProgress(step, static_cast<int>(linuxRestorePlan().size()), QStringLiteral("Working"));
        }
        QVERIFY(!cancel->isEnabled());
        QVERIFY(textOf<QLabel>(dialog, "restoreNotice").contains(QStringLiteral("rewritten")));
        dialog.finishSuccess(QStringLiteral("Ready"));

        RestoreDialog writingDialog(exampleRequest(), linuxRestorePlan());
        confirmErase(writingDialog);
        auto *writingCancel = find<QPushButton>(writingDialog, "cancelRestore");
        for (int step = 1; step < firstDestructiveStepOf(linuxRestorePlan()); ++step) {
            writingDialog.setProgress(step, static_cast<int>(linuxRestorePlan().size()), QStringLiteral("Preparing"));
            QVERIFY(writingCancel->isEnabled());
        }
        writingDialog.setProgress(
            firstDestructiveStepOf(linuxRestorePlan()),
            static_cast<int>(linuxRestorePlan().size()),
            QStringLiteral("Writing"));
        QVERIFY(!writingCancel->isEnabled());
    }

    void keepsCancellationNoticeThroughStallNotices()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        dialog.setProgress(1, static_cast<int>(linuxRestorePlan().size()), QStringLiteral("Checking"));
        auto *notice = find<QLabel>(dialog, "restoreNotice");
        const QString stall =
            QStringLiteral("No response from the device for 0:20. Keep it connected while the system waits.");
        dialog.setNotice(stall);
        QCOMPARE(notice->text(), stall);

        dialog.setCancelling();
        const QString cancellation = notice->text();
        QVERIFY(cancellation.contains(QStringLiteral("checkpoint")));
        QCOMPARE(textOf<QPushButton>(dialog, "cancelRestore"), QStringLiteral("Stopping…"));

        dialog.setNotice(
            QStringLiteral("No response from the device for 1:05. Keep it connected while the system waits."));
        QCOMPARE(notice->text(), cancellation);
        dialog.setNotice(
            QStringLiteral("The device is responding again. Keep it connected until the restore finishes."));
        QCOMPARE(notice->text(), cancellation);
        QVERIFY(!notice->text().contains(QStringLiteral("until the restore finishes")));
    }

    void keepsElapsedTimeAndStopsAnimationAtTheResult()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        auto *timer = dialog.findChild<QTimer *>();
        QVERIFY(timer->isActive());
        QTRY_VERIFY(textOf<QLabel>(dialog, "restoreClock") != QStringLiteral("Elapsed 0:00"));
        dialog.finishFailure(QStringLiteral("Example failure during setup"));
        QVERIFY(!timer->isActive());
        QCOMPARE(dialog.findChild<QProgressBar *>()->value(), 0);
    }

    void keepsLongErrorsScrollableInACompactWindow()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        dialog.resize(480, 560);
        dialog.show();
        dialog.finishFailure(QStringLiteral("Formatter output: an operation failed. ").repeated(100));
        QApplication::processEvents();
        auto *message = find<QLabel>(dialog, "restoreMessage");
        auto *area = qobject_cast<QScrollArea *>(message->parentWidget()->parentWidget());
        QVERIFY(area);
        QVERIFY(area->height() <= 140);
        QVERIFY(message->height() > area->height());
        QVERIFY(find<QPushButton>(dialog, "restoreDone")->isVisible());
    }

    void failureAndCancellationDoNotCompleteTheCurrentStep_data()
    {
        QTest::addColumn<bool>("failed");
        QTest::newRow("failed") << true;
        QTest::newRow("cancelled") << false;
    }

    void failureAndCancellationDoNotCompleteTheCurrentStep()
    {
        QFETCH(bool, failed);
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        confirmErase(dialog);
        dialog.show();
        dialog.setProgress(1, 10, QStringLiteral("Checking"));
        dialog.setProgress(2, 10, QStringLiteral("Identifying"));
        if (failed) {
            dialog.finishFailure(QStringLiteral("Drive <example> stopped responding. Reconnect and try again."));
        } else {
            dialog.finishCancelled(QStringLiteral("The disk was not erased."));
        }
        QCOMPARE(stepRow(dialog, 1)->property("state").toString(), QStringLiteral("complete"));
        QCOMPARE(
            stepRow(dialog, 2)->property("state").toString(),
            failed ? QStringLiteral("error") : QStringLiteral("cancelled"));
        QCOMPARE(stepRow(dialog, 3)->property("state").toString(), QStringLiteral("pending"));
        QCOMPARE(dialog.findChild<QProgressBar *>()->value(), 1);
        QCOMPARE(find<QLabel>(dialog, "restoreMessage")->textFormat(), Qt::PlainText);
        QVERIFY(find<QPushButton>(dialog, "restoreDone")->isVisible());
        QVERIFY(!find<QPushButton>(dialog, "cancelRestore")->isVisible());
    }

    void requiresEraseAcknowledgementBeforeShowingProgress_data()
    {
        QTest::addColumn<bool>("acknowledge");
        QTest::addColumn<int>("outcome");
        QTest::newRow("decline") << false << 0;
        QTest::newRow("success") << true << 0;
        QTest::newRow("failure") << true << 1;
        QTest::newRow("cancellation") << true << 2;
    }

    void dismissingConfirmationNeverStartsARestore_data()
    {
        QTest::addColumn<QString>("action");
        QTest::addColumn<bool>("checked");
        for (const QString &action :
             {QStringLiteral("cancel"),
              QStringLiteral("escape"),
              QStringLiteral("close"),
              QStringLiteral("accept"),
              QStringLiteral("enter")}) {
            for (bool checked : {false, true}) {
                QTest::newRow(qPrintable(action + (checked ? "-checked" : "-unchecked"))) << action << checked;
            }
        }
    }

    void dismissingConfirmationNeverStartsARestore()
    {
        QFETCH(QString, action);
        QFETCH(bool, checked);
        ExampleService service;
        MainWindow window(service, Theme::Light);
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "startRestore", Qt::DirectConnection));
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        QSignalSpy confirmed(dialog, &RestoreDialog::restoreConfirmed);
        dialog->findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))->setChecked(checked);
        if (action == QStringLiteral("cancel")) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
        } else if (action == QStringLiteral("escape")) {
            QTest::keyClick(dialog, Qt::Key_Escape);
        } else if (action == QStringLiteral("close")) {
            dialog->close();
        } else if (action == QStringLiteral("accept")) {
            dialog->accept();
        } else {
            QTest::keyClick(dialog, Qt::Key_Return);
        }
        QVERIFY(!dialog->isVisible());
        dialog->findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))->setChecked(!checked);
        dialog->findChild<QPushButton *>(QStringLiteral("confirmErase"))->click();
        QCOMPARE(confirmed.count(), 0);
        QCOMPARE(service.restores.load(), 0);
    }

    void confirmationShowsTheEntireErasePlanInPlainText()
    {
        auto request = exampleRequest();
        request.disk.size = 2ULL * 1024 * 1024 * 1024 * 1024;
        request.disk.mountPoints = {QStringLiteral("/media/important")};
        request.disk.labels = {QStringLiteral("<b>Backups</b>")};
        request.style = PartitionStyle::Mbr;
        request.fileSystem = FileSystemType::Fat32;
        request.volumeLabel = QStringLiteral("NEWUSB");
        RestoreDialog dialog(request, linuxRestorePlan());
        dialog.show();
        auto *contents = find<QLabel>(dialog, "eraseContents");
        QCOMPARE(contents->textFormat(), Qt::PlainText);
        QVERIFY(contents->text().contains(request.disk.mountPoints.front()));
        QVERIFY(contents->text().contains(request.disk.labels.front()));
        const QString plan = textOf<QLabel>(dialog, "erasePlan");
        QVERIFY(plan.contains(QStringLiteral("MBR")));
        QVERIFY(plan.contains(QStringLiteral("FAT32")));
        QVERIFY(plan.contains(request.volumeLabel));
        bool warned = false;
        for (auto *title : dialog.findChildren<QLabel *>(QStringLiteral("warningTitle"))) {
            warned |= title->text() == largeRestoreTargetWarning(request.disk);
        }
        QVERIFY(warned);
        dialog.setProgress(1, 10, QStringLiteral("Unexpected progress"));
        dialog.finishSuccess(QStringLiteral("Unexpected success"));
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Confirm USB restore"));
        QCOMPARE(dialog.findChild<QProgressBar *>()->value(), 0);
        QVERIFY(!dialog.findChild<QTimer *>()->isActive());
    }

    void confirmsWithKeyboardOnlyAfterAcknowledgement()
    {
        RestoreDialog dialog(exampleRequest(), linuxRestorePlan());
        dialog.show();
        QSignalSpy confirmed(&dialog, &RestoreDialog::restoreConfirmed);
        auto *checkbox = find<QCheckBox>(dialog, "eraseAcknowledgement");
        auto *erase = find<QPushButton>(dialog, "confirmErase");
        checkbox->setFocus();
        QTest::keyClick(checkbox, Qt::Key_Space);
        QVERIFY(checkbox->isChecked());
        QVERIFY(erase->isEnabled());
        QCOMPARE(confirmed.count(), 0);
        erase->setFocus();
        QTest::keyClick(erase, Qt::Key_Space);
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(dialog.windowTitle(), QStringLiteral("Restoring USB drive"));
        dialog.finishCancelled(QStringLiteral("Test ended"));
    }

    void restoresTheRequestShownInTheConfirmation()
    {
        ExampleService service;
        MainWindow window(service, Theme::Light);
        window.show();
        QComboBox *style = nullptr;
        for (auto *combo : window.findChildren<QComboBox *>()) {
            if (combo->accessibleName() == QStringLiteral("Partition style")) {
                style = combo;
            }
        }
        QVERIFY(style);
        style->setCurrentIndex(style->findData(static_cast<int>(PartitionStyle::Gpt)));
        QVERIFY(QMetaObject::invokeMethod(&window, "startRestore", Qt::DirectConnection));
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("erasePlan"))->text().contains(QStringLiteral("GPT")));
        style->setCurrentIndex(style->findData(static_cast<int>(PartitionStyle::Mbr)));
        confirmErase(*dialog);
        QTRY_COMPARE(dialog->windowTitle(), QStringLiteral("Restore complete"));
        QCOMPARE(service.received.style, PartitionStyle::Gpt);
        QCOMPARE(service.received.disk.deviceId, exampleDisk().deviceId);
        QCOMPARE(service.received.volumeLabel, QStringLiteral("USB"));
        dialog->accept();
        window.close();
    }

    void requiresEraseAcknowledgementBeforeShowingProgress()
    {
        QFETCH(bool, acknowledge);
        QFETCH(int, outcome);
        ExampleService service;
        service.outcome = outcome;
        MainWindow window(service, Theme::Light);
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "startRestore", Qt::DirectConnection));
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        auto *erase = dialog->findChild<QPushButton *>(QStringLiteral("confirmErase"));
        auto *acknowledgement = dialog->findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"));
        QVERIFY(!erase->isEnabled());
        QVERIFY(!dialog->findChild<QTimer *>()->isActive());
        erase->click();
        QCOMPARE(service.restores.load(), 0);
        QSignalSpy dismissed(dialog, &QDialog::finished);
        QSignalSpy confirmed(dialog, &RestoreDialog::restoreConfirmed);
        if (!acknowledge) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
            QCOMPARE(service.restores.load(), 0);
            QCOMPARE(confirmed.count(), 0);
            QVERIFY(!dialog->isVisible());
            return;
        }
        acknowledgement->setChecked(true);
        QVERIFY(erase->isEnabled());
        QCOMPARE(service.restores.load(), 0);
        acknowledgement->setChecked(false);
        QVERIFY(!erase->isEnabled());
        confirmErase(*dialog);
        QCOMPARE(confirmed.count(), 1);
        QCOMPARE(dismissed.count(), 0);
        QCOMPARE(window.findChild<RestoreDialog *>(), dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(!erase->isVisible());
        erase->click();
        QCOMPARE(confirmed.count(), 1);
        if (outcome == 2) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
        }
        const QString title = outcome == 0   ? QStringLiteral("Restore complete")
                              : outcome == 1 ? QStringLiteral("Restore failed")
                                             : QStringLiteral("Restore cancelled");
        QTRY_COMPARE(dialog->windowTitle(), title);
        QCOMPARE(service.restores.load(), 1);
        if (outcome == 0) {
            QVERIFY(dialog->findChild<QLabel *>(QStringLiteral("restoreMessage"))
                        ->text()
                        .contains(QStringLiteral("/dev/example1")));
        }
        dialog->accept();
        window.close();
    }

    void returnsToSetupAfterEachOutcome_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::addColumn<bool>("dark");
        for (int outcome : {0, 1, 2}) {
            for (bool dark : {false, true}) {
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(outcome).arg(dark ? "dark" : "light")))
                    << outcome << dark;
            }
        }
    }

    void returnsToSetupAfterEachOutcome()
    {
        QFETCH(int, outcome);
        QFETCH(bool, dark);
        ExampleService service;
        service.outcome = outcome;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        auto *restore = find<QPushButton>(window, "restoreUsb");
        auto *status = find<QLabel>(window, "statusBadge");
        auto *disks = window.findChild<QListWidget *>();
        const QString readyStatus = status->text();
        QVERIFY(readyStatus.contains(QStringLiteral("Ready to restore")));
        QVERIFY(window.findChildren<QProgressBar *>().isEmpty());
        QVERIFY(restore->isEnabled());
        restore->click();
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        confirmErase(*dialog);
        QVERIFY(!restore->isEnabled());
        QCOMPARE(restore->text(), QStringLiteral("Restore USB"));
        QVERIFY(!disks->isEnabled());
        if (outcome == 2) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
        }
        const QString title = outcome == 0   ? QStringLiteral("Restore complete")
                              : outcome == 1 ? QStringLiteral("Restore failed")
                                             : QStringLiteral("Restore cancelled");
        QTRY_COMPARE(dialog->windowTitle(), title);
        QVERIFY(dialog->isVisible());
        QCOMPARE(status->text(), readyStatus);
        QCOMPARE(service.restores.load(), 1);
        const QString message = dialog->findChild<QLabel *>(QStringLiteral("restoreMessage"))->text();
        QVERIFY(!message.isEmpty());
        if (outcome == 1) {
            QCOMPARE(message, QStringLiteral("Example restore failure"));
        }
        for (auto *label : window.findChildren<QLabel *>()) {
            if (!dialog->isAncestorOf(label)) {
                QVERIFY(!label->text().contains(title));
                QVERIFY(!label->text().contains(message));
            }
        }
        QCOMPARE(window.findChildren<QProgressBar *>().size(), 1);
        QVERIFY(dialog->isAncestorOf(window.findChild<QProgressBar *>()));
        dialog->findChild<QPushButton *>(QStringLiteral("restoreDone"))->click();
        QVERIFY(!dialog->isVisible());
        QCOMPARE(status->text(), readyStatus);
        QVERIFY(restore->isEnabled());
        QVERIFY(disks->isEnabled());
        for (auto *combo : window.findChildren<QComboBox *>()) {
            QVERIFY(combo->isEnabled());
        }
        QTRY_VERIFY(window.findChildren<QThread *>().isEmpty());

        service.outcome = 0;
        restore->click();
        dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Confirm USB restore"));
        QVERIFY(!dialog->findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))->isChecked());
        auto *erase = dialog->findChild<QPushButton *>(QStringLiteral("confirmErase"));
        QVERIFY(!erase->isEnabled());
        erase->click();
        QCOMPARE(service.restores.load(), 1);
        confirmErase(*dialog);
        QTRY_COMPARE(dialog->windowTitle(), QStringLiteral("Restore complete"));
        QCOMPARE(service.restores.load(), 2);
        QCOMPARE(status->text(), readyStatus);
        dialog->accept();
        window.close();
    }

    void keepsFreshScanProblemsSeparateFromTheRestoreOutcome_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::addColumn<QString>("problem");
        for (int outcome : {0, 1, 2}) {
            for (const QString &problem :
                 {QStringLiteral("missing"), QStringLiteral("unsafe"), QStringLiteral("error")}) {
                QTest::newRow(qPrintable(QStringLiteral("%1-%2").arg(outcome).arg(problem))) << outcome << problem;
            }
        }
    }

    void keepsFreshScanProblemsSeparateFromTheRestoreOutcome()
    {
        QFETCH(int, outcome);
        QFETCH(QString, problem);
        ExampleService service;
        service.outcome = outcome;
        MainWindow window(service, Theme::Light);
        window.show();
        auto *restore = find<QPushButton>(window, "restoreUsb");
        restore->click();
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        if (problem == QStringLiteral("missing")) {
            service.diskPresent = false;
        } else if (problem == QStringLiteral("unsafe")) {
            service.disk.isReadOnly = true;
        } else {
            service.scanError = QStringLiteral("Example scan failure");
        }
        confirmErase(*dialog);
        if (outcome == 2) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
        }
        QTRY_VERIFY(dialog->findChild<QPushButton *>(QStringLiteral("restoreDone"))->isVisible());
        const QString status = textOf<QLabel>(window, "statusBadge");
        QVERIFY(status.contains(
            problem == QStringLiteral("missing")  ? QStringLiteral("Select a USB disk")
            : problem == QStringLiteral("unsafe") ? QStringLiteral("This disk will not be restored")
                                                  : QStringLiteral("Could not scan for USB disks")));
        QVERIFY(!restore->isEnabled());
        dialog->accept();
        QVERIFY(!restore->isEnabled());
        window.close();
    }

    void doesNotRepurposePaletteRolesForTheStepIndicator()
    {
        for (const Theme theme : {Theme::Light, Theme::Dark}) {
            applyTheme(*qApp, theme);
            const Palette &colors = paletteFor(resolveColorScheme(theme));
            QCOMPARE(appliedPalette().border, colors.border);
            QCOMPARE(appliedPalette().statusError, colors.statusError);
            QCOMPARE(appliedPalette().accent, colors.accent);
            QVERIFY(qApp->palette().color(QPalette::Mid) != colors.border);
            QVERIFY(qApp->palette().color(QPalette::LinkVisited) != colors.statusError);
        }
    }

    void rendersBothPlansAndThemes_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("windowsPlan");
        QTest::addColumn<QString>("outcome");
        QTest::addColumn<Language>("language");
        for (bool dark : {false, true}) {
            for (bool windowsPlan : {false, true}) {
                for (const QString &outcome :
                     {QStringLiteral("confirmation"),
                      QStringLiteral("acknowledged"),
                      QStringLiteral("running"),
                      QStringLiteral("success"),
                      QStringLiteral("failed"),
                      QStringLiteral("cancelled")}) {
                    const QString name = QStringLiteral("%1-%2-%3")
                                             .arg(dark ? "dark" : "light", windowsPlan ? "windows" : "linux", outcome);
                    QTest::newRow(qPrintable(name)) << dark << windowsPlan << outcome << Language::English;
                    QTest::newRow(qPrintable(name + QStringLiteral("-de")))
                        << dark << windowsPlan << outcome << Language::German;
                }
            }
        }
    }

    void rendersBothPlansAndThemes()
    {
        QFETCH(bool, dark);
        QFETCH(bool, windowsPlan);
        QFETCH(QString, outcome);
        QFETCH(Language, language);
        Translations translations;
        QVERIFY(translations.load(language));
        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        const auto &plan = windowsPlan ? windowsRestorePlan() : linuxRestorePlan();
        RestoreDialog dialog(exampleRequest(), plan);
        dialog.show();
        auto *acknowledgement = find<QCheckBox>(dialog, "eraseAcknowledgement");
        auto *erase = find<QPushButton>(dialog, "confirmErase");
        QVERIFY(!acknowledgement->isChecked());
        QVERIFY(!erase->isEnabled());
        if (language == Language::German) {
            QCOMPARE(dialog.windowTitle(), QStringLiteral("USB-Wiederherstellung bestätigen"));
            QCOMPARE(erase->text(), QStringLiteral("Löschen und wiederherstellen"));
            QCOMPARE(textOf<QLabel>(dialog, "restoreStepTitle"), QStringLiteral("Laufwerkssicherheit prüfen"));
            QVERIFY(acknowledgement->text().contains(QStringLiteral("USB drive 2")));
            QVERIFY(acknowledgement->text().contains(QStringLiteral("unwiderruflich gelöscht")));
        }
        const bool confirming = outcome == QStringLiteral("confirmation") || outcome == QStringLiteral("acknowledged");
        if (!confirming) {
            confirmErase(dialog);
        } else if (outcome == QStringLiteral("acknowledged")) {
            find<QCheckBox>(dialog, "eraseAcknowledgement")->setChecked(true);
        }
        const int lastStep = confirming ? 0 : outcome == QStringLiteral("success") ? static_cast<int>(plan.size()) : 3;
        for (int step = 1; step <= lastStep; ++step) {
            dialog.setProgress(
                step,
                static_cast<int>(plan.size()),
                language == Language::German ? QStringLiteral("Das Laufwerk wird für einen Neuanfang vorbereitet…")
                                             : QStringLiteral("Preparing the drive for a fresh start…"));
        }
        if (outcome == QStringLiteral("success")) {
            dialog.finishSuccess(
                language == Language::German
                    ? QStringLiteral("Der Datenträger enthält jetzt ein exFAT-Volume namens USB unter /dev/example1.")
                    : QStringLiteral("The disk is now one exFAT volume labelled USB at /dev/example1."));
        } else if (outcome == QStringLiteral("failed")) {
            dialog.finishFailure(
                language == Language::German
                    ? QStringLiteral(
                          "USB drive 2 konnte nicht beschrieben werden: Das Laufwerk reagiert nicht mehr (Fehler 5). "
                          "Ziehen Sie das Laufwerk ab, schließen Sie es wieder an und versuchen Sie es erneut. "
                          "Versuchen Sie bei weiteren Fehlern einen anderen USB-Anschluss oder ersetzen Sie das "
                          "Laufwerk.")
                    : QStringLiteral(
                          "Could not write to USB drive 2: the drive stopped responding (error 5). "
                          "Unplug the drive, plug it back in and try again. If it keeps failing, "
                          "try another USB port or replace the drive."));
        } else if (outcome == QStringLiteral("cancelled")) {
            dialog.finishCancelled(
                language == Language::German ? QStringLiteral("Die Wiederherstellung wurde vor dem Löschen angehalten.")
                                             : QStringLiteral("The restore stopped before erasing the disk."));
        }
        QTest::qWait(80);
        settleLayout(dialog);
        if (!confirming) {
            const auto *messageArea = find<QScrollArea>(dialog, "restoreMessageArea");
            const auto *counter = find<QLabel>(dialog, "restoreCounter");
            const int messageBottom = messageArea->mapTo(&dialog, messageArea->rect().bottomLeft()).y();
            const int counterTop = counter->mapTo(&dialog, QPoint()).y();
            QVERIFY(messageBottom < counterTop);
        }
        for (auto *button : dialog.findChildren<QPushButton *>()) {
            if (button->isVisible()) {
                QVERIFY2(
                    button->fontMetrics().horizontalAdvance(button->text()) < button->width(),
                    qPrintable(button->text()));
            }
        }
        auto *done = find<QPushButton>(dialog, "restoreDone");
        QVERIFY(done);
        if (done->isVisible()) {
            QVERIFY(done->geometry().bottom() < dialog.height());
        }
        QVERIFY(
            find<QLabel>(dialog, "restoreTitle")->height() >=
            find<QLabel>(dialog, "restoreTitle")->fontMetrics().height());
        const QString output = qEnvironmentVariable("USBRESTORE_GUI_SNAPSHOTS");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(dialog.grab().save(QDir(output).filePath(QString::fromLatin1(QTest::currentDataTag()) + ".png")));
        }
        dialog.finishCancelled(QStringLiteral("Preview ended"));
    }

    void rendersTheMainWindow_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<Language>("language");
        QTest::newRow("light") << false << Language::English;
        QTest::newRow("dark") << true << Language::English;
        QTest::newRow("light-de") << false << Language::German;
        QTest::newRow("dark-de") << true << Language::German;
    }

    // Saves the README's interface preview: USBRESTORE_GUI_SNAPSHOTS=docs/images.
    void rendersTheMainWindow()
    {
        QFETCH(bool, dark);
        QFETCH(Language, language);
        Translations translations;
        QVERIFY(translations.load(language));
        QSettings(QStringLiteral("KaroqDave"), QStringLiteral("USB Restoration Tool")).clear();
        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        ExampleService service;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        QTest::qWait(80);
        settleLayout(window);
        auto *restore = find<QPushButton>(window, "restoreUsb");
        QVERIFY(restore);
        QVERIFY(restore->isEnabled());
        QCOMPARE(
            restore->text(),
            language == Language::German ? QStringLiteral("USB wiederherstellen") : QStringLiteral("Restore USB"));
        QCOMPARE(
            textOf<QLabel>(window, "warningTitle"),
            language == Language::German
                ? QStringLiteral(
                      "Die Wiederherstellung löscht alle Partitionen und Dateien auf dem ausgewählten Laufwerk.")
                : QStringLiteral("Restoring erases every partition and file on the selected drive."));
        QCOMPARE(
            themeButtonText(Theme::System),
            language == Language::German ? QStringLiteral("Design: Automatisch") : QStringLiteral("Theme: Auto"));
        auto *languageButton = find<QPushButton>(window, "language");
        QVERIFY(languageButton);
        QCOMPARE(languageButton->text(), languageButtonText(Language::System));
        for (const QString &name :
             {QStringLiteral("activity"),
              QStringLiteral("theme"),
              QStringLiteral("language"),
              QStringLiteral("about")}) {
            auto *button = window.findChild<QPushButton *>(name);
            QVERIFY(button);
            QVERIFY(button->isVisible());
            QVERIFY2(
                button->fontMetrics().horizontalAdvance(button->text()) < button->width(), qPrintable(button->text()));
        }
        const QString output = qEnvironmentVariable("USBRESTORE_GUI_SNAPSHOTS");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(window.grab().save(
                QDir(output).filePath(QStringLiteral("app-%1.png").arg(QString::fromLatin1(QTest::currentDataTag())))));
        }
        window.close();
    }

    void translatesMainWindowDialogsAndErrors_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void translatesMainWindowDialogsAndErrors()
    {
        QFETCH(bool, dark);
        Translations translations;
        QVERIFY(translations.load(Language::German));
        ExampleService service;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "showActivity"));
        QDialog *activity = nullptr;
        for (auto *dialog : window.findChildren<QDialog *>()) {
            if (dialog->windowTitle() == QStringLiteral("Aktivität")) {
                activity = dialog;
            }
        }
        QVERIFY(activity);
        QVERIFY(activity->isVisible());
        bool hasOpenLog = false;
        for (auto *button : activity->findChildren<QPushButton *>()) {
            hasOpenLog = hasOpenLog || button->text() == QStringLiteral("Protokolldatei öffnen");
        }
        QVERIFY(hasOpenLog);
        activity->hide();

        QString aboutTitle;
        QString aboutDetails;
        QTimer::singleShot(0, &window, [&] {
            if (auto *box = find<QMessageBox>(window, "aboutDialog")) {
                aboutTitle = box->windowTitle();
                aboutDetails = box->informativeText();
                box->accept();
            }
        });
        find<QPushButton>(window, "about")->click();
        QCOMPARE(aboutTitle, QStringLiteral("Über USB Restoration Tool"));
        QVERIFY(aboutDetails.contains(QStringLiteral("Entwickelt von KaroqDave")));
        QVERIFY(aboutDetails.contains(QStringLiteral("Kontrollkästchen aktivieren und bestätigen")));

        find<QPushButton>(window, "restoreUsb")->click();
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("confirmErase"))->isEnabled());
        dialog->reject();
        QCOMPARE(service.restores.load(), 0);

        service.scanError = QStringLiteral("Example scan failure: errno 5");
        QVERIFY(QMetaObject::invokeMethod(&window, "refreshDisks"));
        QVERIFY(
            find<QLabel>(window, "statusBadge")
                ->text()
                .contains(QStringLiteral("Die Suche nach USB-Laufwerken ist fehlgeschlagen.")));
        QVERIFY(
            find<QTextEdit>(window, "activityLog")
                ->toPlainText()
                .contains(QStringLiteral("Scan failed: Example scan failure: errno 5")));
        QVERIFY(!find<QPushButton>(window, "restoreUsb")->isEnabled());
        QCOMPARE(service.restores.load(), 0);
        window.close();
    }

    void translatesRestoreResultsSuppliedByMainWindow_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::newRow("success") << 0;
        QTest::newRow("failed") << 1;
        QTest::newRow("cancelled") << 2;
    }

    void translatesRestoreResultsSuppliedByMainWindow()
    {
        QFETCH(int, outcome);
        Translations translations;
        QVERIFY(translations.load(Language::German));
        ExampleService service;
        service.outcome = outcome;
        if (outcome == 1) {
            service.reportMessages = true;
            service.backendError = restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "The restore failed."));
        }
        MainWindow window(service, Theme::Light, &translations);
        window.show();
        find<QPushButton>(window, "restoreUsb")->click();
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        confirmErase(*dialog);
        if (outcome == 2) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
        }
        const QString title = outcome == 0   ? QStringLiteral("Wiederherstellung abgeschlossen")
                              : outcome == 1 ? QStringLiteral("Wiederherstellung fehlgeschlagen")
                                             : QStringLiteral("Wiederherstellung abgebrochen");
        const QString message =
            outcome == 0
                ? QStringLiteral("Das Laufwerk enthält jetzt ein exFAT-Volume mit dem Namen USB unter /dev/example1.")
            : outcome == 1 ? QStringLiteral("Die Wiederherstellung ist fehlgeschlagen.")
                           : QStringLiteral("Die Wiederherstellung wurde vor dem Löschen des Laufwerks angehalten.");
        QTRY_COMPARE(dialog->windowTitle(), title);
        QCOMPARE(dialog->findChild<QLabel *>(QStringLiteral("restoreMessage"))->text(), message);
        QCOMPARE(service.restores.load(), 1);
        QCOMPARE(service.received.displayLanguage, Language::German);
        const QString log = find<QTextEdit>(window, "activityLog")->toPlainText();
        QVERIFY(log.contains(QStringLiteral("Restore requested for USB drive 2 — Example Flash Drive (32.00 GB)")));
        QVERIFY(!log.contains(QStringLiteral("32,00 GB")));
        if (outcome == 1) {
            QVERIFY(log.contains(QStringLiteral("ERROR: The restore failed.")));
            QVERIFY(!log.contains(QStringLiteral("Die Wiederherstellung ist fehlgeschlagen.")));
        }
        dialog->accept();
        window.close();
    }

    void rendersTheConfirmationDialog_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    // Saves the README's confirmation preview, opened through the main window as a user would.
    void rendersTheConfirmationDialog()
    {
        QFETCH(bool, dark);
        QSettings(QStringLiteral("KaroqDave"), QStringLiteral("USB Restoration Tool")).clear();
        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        ExampleService service;
        service.disk = installerDisk();
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        find<QPushButton>(window, "restoreUsb")->click();
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        QVERIFY(dialog->isVisible());
        QVERIFY(!dialog->findChild<QPushButton *>(QStringLiteral("confirmErase"))->isEnabled());
        QTest::qWait(80);

        const QString output = qEnvironmentVariable("USBRESTORE_GUI_SNAPSHOTS");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(dialog->grab().save(
                QDir(output).filePath(QStringLiteral("confirm-%1.png").arg(dark ? "dark" : "light"))));
        }
        dialog->reject();
        QCOMPARE(service.restores.load(), 0);
        window.close();
    }

    void rendersTheRestoreAnimation_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    // Records the README's restore animation; scripts/render-restore-animation.sh encodes it at the same rate.
    void rendersTheRestoreAnimation()
    {
        QFETCH(bool, dark);
        const QString output = qEnvironmentVariable("USBRESTORE_GUI_ANIMATION");
        if (output.isEmpty()) {
            QSKIP("Set USBRESTORE_GUI_ANIMATION to a directory to record the frames; it takes real time.");
        }
        constexpr int FrameMs = 50;
        QDir frames(QDir(output).filePath(QString::fromLatin1(QTest::currentDataTag())));
        QVERIFY(frames.removeRecursively());
        QVERIFY(QDir().mkpath(frames.path()));

        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        RestoreRequest request = exampleRequest();
        request.disk = installerDisk();
        const QVector<RestoreStep> &plan = linuxRestorePlan();
        RestoreDialog dialog(request, plan);
        dialog.show();

        // The backend's own messages, so the preview reads like a real Linux restore.
        const auto message = [](RestoreStep step) {
            switch (step) {
            case RestoreStep::VerifyDisk:
                return QStringLiteral("Verifying the selected USB disk");
            case RestoreStep::IdentifyDevice:
                return QStringLiteral("Identifying the block device");
            case RestoreStep::Unmount:
                return QStringLiteral("Unmounting existing filesystems");
            case RestoreStep::OpenExclusive:
                return QStringLiteral("Opening the device exclusively");
            case RestoreStep::ClearSignatures:
                return QStringLiteral("Clearing old MBR, GPT and ISO signatures");
            case RestoreStep::WritePartitionTable:
                return QStringLiteral("Writing the GPT partition table");
            case RestoreStep::Format:
                return QStringLiteral("Formatting as exFAT");
            case RestoreStep::RereadPartitionTable:
                return QStringLiteral("Re-reading the partition table");
            case RestoreStep::WaitForPartition:
                return QStringLiteral("Waiting for the new partition to appear");
            default:
                return QStringLiteral("Restore complete");
            }
        };

        struct Cue {
            qint64 atMs;
            std::function<void()> act;
        };
        QVector<Cue> cues;
        cues.append({1400, [&] { find<QCheckBox>(dialog, "eraseAcknowledgement")->setChecked(true); }});
        cues.append({2400, [&] { find<QPushButton>(dialog, "confirmErase")->click(); }});
        qint64 at = 3000;
        const int total = static_cast<int>(plan.size());
        for (int step = 1; step <= total; ++step) {
            cues.append({at, [&dialog, &plan, message, step, total] {
                             QVERIFY(dialog.setProgress(step, total, message(plan.at(step - 1))));
                         }});
            // Clearing signatures is the one step that takes noticeably longer on a real stick.
            at += plan.at(step - 1) == RestoreStep::ClearSignatures ? 1600 : 500;
        }
        cues.append({at, [&] {
                         dialog.finishSuccess(
                             QStringLiteral("The disk is now one exFAT volume labelled USB at /dev/example1."));
                     }});
        const qint64 end = at + 3000;

        QElapsedTimer clock;
        clock.start();
        int frame = 0;
        int next = 0;
        for (qint64 due = 0; due <= end; due += FrameMs) {
            QTest::qWait(static_cast<int>(qMax<qint64>(0, due - clock.elapsed())));
            while (next < cues.size() && cues.at(next).atMs <= due) {
                cues.at(next++).act();
            }
            QCoreApplication::processEvents();
            QVERIFY(dialog.grab().save(
                frames.filePath(QStringLiteral("frame-%1.png").arg(frame++, 4, 10, QLatin1Char('0')))));
        }
        QCOMPARE(next, cues.size());
    }

    void describesTheReleaseAndProjectVersion_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    void describesTheReleaseAndProjectVersion()
    {
        QFETCH(bool, dark);
        ExampleService service;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        const QString version = QString::fromLatin1(USBRESTORE_APP_VERSION);
        QCOMPARE(textOf<QLabel>(window, "version"), QStringLiteral("v%1").arg(version));

        QString title;
        QString details;
        bool hasGithub = false;
        QTimer::singleShot(0, [&] {
            auto *box = find<QMessageBox>(window, "aboutDialog");
            if (!box) {
                return;
            }
            title = box->text();
            details = box->informativeText();
            for (auto *button : box->findChildren<QPushButton *>()) {
                hasGithub = hasGithub || button->text() == QStringLiteral("Open GitHub");
            }
            box->accept();
        });
        find<QPushButton>(window, "about")->click();

        QCOMPARE(title, QStringLiteral("USB Restoration Tool %1").arg(version));
        QVERIFY(version != QStringLiteral("0.0.0-dev"));
        QVERIFY(details.contains(QStringLiteral("Created by KaroqDave")));
        QVERIFY(details.contains(QStringLiteral("one window")));
        QVERIFY(details.contains(QStringLiteral("tick the acknowledgement and confirm")));
        QVERIFY(details.contains(QStringLiteral("boot, system, offline, and read-only")));
        QVERIFY(details.contains(QStringLiteral("Windows portable package")));
        QVERIFY(details.contains(QStringLiteral("Debian package")));
        QVERIFY(details.contains(QStringLiteral("Fedora RPM")));
        QVERIFY(details.contains(QStringLiteral("Linux AppImage")));
        QVERIFY(details.contains(QStringLiteral("ext4")));
        QVERIFY(hasGithub);
        window.close();
    }

    void disablesCancellationAtTheFirstWrite_data()
    {
        QTest::addColumn<bool>("windowsPlan");
        QTest::newRow("linux") << false;
        QTest::newRow("windows") << true;
    }

    void disablesCancellationAtTheFirstWrite()
    {
        QFETCH(bool, windowsPlan);
        const QVector<RestoreStep> &plan = windowsPlan ? windowsRestorePlan() : linuxRestorePlan();
        const int total = static_cast<int>(plan.size());
        const int destructive = firstDestructiveStepOf(plan);
        RestoreDialog dialog(exampleRequest(), plan);
        confirmErase(dialog);
        auto *cancel = find<QPushButton>(dialog, "cancelRestore");
        QSignalSpy requested(&dialog, &RestoreDialog::cancelRequested);
        for (int step = 1; step < destructive; ++step) {
            dialog.setProgress(step, total, QStringLiteral("Preparing"));
            QVERIFY(cancel->isEnabled());
        }
        dialog.setProgress(destructive, total, QStringLiteral("Writing"));
        QVERIFY(!cancel->isEnabled());
        QCOMPARE(cancel->text(), QStringLiteral("Writing in progress"));
        cancel->click();
        QCOMPARE(requested.count(), 0);
        QVERIFY(textOf<QLabel>(dialog, "restoreNotice").contains(QStringLiteral("rewritten")));
    }

    void keepsTheEraseGateInsideTheDialog_data()
    {
        QTest::addColumn<bool>("dark");
        QTest::addColumn<bool>("windowsPlan");
        for (bool dark : {false, true}) {
            for (bool windowsPlan : {false, true}) {
                QTest::newRow(
                    qPrintable(QStringLiteral("%1-%2").arg(dark ? "dark" : "light", windowsPlan ? "windows" : "linux")))
                    << dark << windowsPlan;
            }
        }
    }

    void keepsTheEraseGateInsideTheDialog()
    {
        QFETCH(bool, dark);
        QFETCH(bool, windowsPlan);
        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        const QVector<RestoreStep> &plan = windowsPlan ? windowsRestorePlan() : linuxRestorePlan();
        RestoreDialog dialog(exampleRequest(), plan);
        dialog.show();
        dialog.resize(dialog.minimumSize());
        QApplication::processEvents();

        const auto inside = [&dialog](QWidget *widget) {
            const QRect rect(widget->mapTo(&dialog, QPoint(0, 0)), widget->size());
            return widget->isVisible() && dialog.rect().contains(rect);
        };
        QVERIFY(inside(find<QCheckBox>(dialog, "eraseAcknowledgement")));
        QVERIFY(inside(find<QPushButton>(dialog, "confirmErase")));
        confirmErase(dialog);
        dialog.resize(dialog.minimumSize());
        QApplication::processEvents();
        QVERIFY(inside(find<QPushButton>(dialog, "cancelRestore")));
        dialog.finishFailure(QStringLiteral("Example failure"));
        QApplication::processEvents();
        QVERIFY(inside(find<QPushButton>(dialog, "restoreDone")));
    }

    void explainsClosingDuringARestore_data()
    {
        QTest::addColumn<int>("outcome");
        QTest::addColumn<bool>("dark");
        QTest::addColumn<QString>("explanation");
        for (bool dark : {false, true}) {
            QTest::newRow(qPrintable(QStringLiteral("before-writing-%1").arg(dark ? "dark" : "light")))
                << 2 << dark << QStringLiteral("The disk has not been changed yet");
            QTest::newRow(qPrintable(QStringLiteral("writing-%1").arg(dark ? "dark" : "light")))
                << 3 << dark << QStringLiteral("leave the disk unusable");
        }
    }

    void explainsClosingDuringARestore()
    {
        QFETCH(int, outcome);
        QFETCH(bool, dark);
        QFETCH(QString, explanation);
        ExampleService service;
        service.outcome = outcome;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        QVERIFY(QMetaObject::invokeMethod(&window, "startRestore", Qt::DirectConnection));
        auto *dialog = window.findChild<RestoreDialog *>();
        QVERIFY(dialog);
        confirmErase(*dialog);
        if (outcome == 3) {
            QTRY_COMPARE(
                dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->text(),
                QStringLiteral("Writing in progress"));
        } else {
            QTRY_VERIFY(service.restores.load() == 1);
        }

        QString boxText;
        QTimer::singleShot(0, [&] {
            QMessageBox *box = nullptr;
            for (QWidget *widget : QApplication::topLevelWidgets()) {
                box = qobject_cast<QMessageBox *>(widget);
                if (box) {
                    break;
                }
            }
            if (!box) {
                box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            }
            if (!box) {
                return;
            }
            boxText = box->text();
            box->accept();
        });
        window.close();

        QVERIFY(boxText.contains(explanation));
        QVERIFY(window.isVisible());
        QVERIFY(dialog->isVisible());
        QCOMPARE(dialog->windowTitle(), QStringLiteral("Restoring USB drive"));
        if (outcome == 2) {
            dialog->findChild<QPushButton *>(QStringLiteral("cancelRestore"))->click();
            QTRY_COMPARE(dialog->windowTitle(), QStringLiteral("Restore cancelled"));
        } else {
            QTRY_COMPARE(dialog->windowTitle(), QStringLiteral("Restore failed"));
            QCOMPARE(
                dialog->findChild<QLabel *>(QStringLiteral("restoreMessage"))->text(),
                QStringLiteral("Example stopped after writing started"));
        }
        dialog->accept();
        window.close();
    }

  private:
    QTemporaryDir m_settings;
};

QTEST_MAIN(GuiTests)
#include "test_gui.moc"

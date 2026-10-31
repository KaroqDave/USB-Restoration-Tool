#include "gui/main_window.h"
#include "gui/restore_dialog.h"
#include "gui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QFrame>
#include <QImage>
#include <QLabel>
#include <QLibraryInfo>
#include <QListWidget>
#include <QMessageBox>
#include <QPalette>
#include <QProcess>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QSettings>
#include <QScrollArea>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>
#include <QThread>
#include <QTimer>

#include <atomic>
#include <functional>

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

void confirmErase(RestoreDialog &dialog)
{
    dialog.findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))->setChecked(true);
    dialog.findChild<QPushButton *>(QStringLiteral("confirmErase"))->click();
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
    bool diskPresent = true;
    QString scanError;

    PrivilegeMode privilegeMode() const override { return PrivilegeMode::Helper; }
    QString privilegeHint() const override { return {}; }
    QVector<DiskInfo> listUsbDisks(QString *error) const override
    {
        *error = scanError;
        return diskPresent && scanError.isEmpty() ? QVector<DiskInfo>{disk} : QVector<DiskInfo>{};
    }
    bool refreshDisk(const DiskInfo &, DiskInfo *refreshedDisk, QString *) const override
    {
        *refreshedDisk = disk;
        return true;
    }
    RestoreGuard restoreGuard() const override { return {}; }
    QVector<FileSystemType> supportedFileSystems() const override { return {FileSystemType::ExFat}; }
    bool canFormatFileSystem(FileSystemType, const DiskInfo &, QString *) const override { return true; }
    const QVector<RestoreStep> &restorePlan() const override { return linuxRestorePlan(); }
    int totalRestoreSteps() const override { return static_cast<int>(restorePlan().size()); }
    int firstDestructiveStep() const override { return firstDestructiveStepOf(restorePlan()); }
    bool
    restore(const RestoreRequest &request, RestoreReporter &reporter, RestoreResult *result, QString *error) override
    {
        received = request;
        ++restores;
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
        QVERIFY(m_settings.isValid());
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
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
        auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("cancelRestore"));
        QSignalSpy requested(&dialog, &RestoreDialog::cancelRequested);
        connect(&dialog, &RestoreDialog::cancelRequested, &dialog, &RestoreDialog::setCancelling);
        dialog.show();
        QTest::mouseClick(cancel, Qt::LeftButton);
        QCOMPARE(requested.count(), 1);
        QVERIFY(!cancel->isEnabled());
        QVERIFY(
            dialog.findChild<QLabel *>(QStringLiteral("restoreNotice"))->text().contains(QStringLiteral("checkpoint")));
        // A request can arrive after the backend's last checkpoint; progress must still be shown.
        for (int step = 1; step <= firstDestructiveStepOf(linuxRestorePlan()); ++step) {
            dialog.setProgress(step, static_cast<int>(linuxRestorePlan().size()), QStringLiteral("Working"));
        }
        QVERIFY(!cancel->isEnabled());
        QVERIFY(
            dialog.findChild<QLabel *>(QStringLiteral("restoreNotice"))->text().contains(QStringLiteral("rewritten")));
        dialog.finishSuccess(QStringLiteral("Ready"));

        RestoreDialog writingDialog(exampleRequest(), linuxRestorePlan());
        confirmErase(writingDialog);
        auto *writingCancel = writingDialog.findChild<QPushButton *>(QStringLiteral("cancelRestore"));
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
        auto *notice = dialog.findChild<QLabel *>(QStringLiteral("restoreNotice"));
        const QString stall =
            QStringLiteral("No response from the device for 0:20. Keep it connected while the system waits.");
        dialog.setNotice(stall);
        QCOMPARE(notice->text(), stall);

        dialog.setCancelling();
        const QString cancellation = notice->text();
        QVERIFY(cancellation.contains(QStringLiteral("checkpoint")));
        QCOMPARE(dialog.findChild<QPushButton *>(QStringLiteral("cancelRestore"))->text(), QStringLiteral("Stopping…"));

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
        QTRY_VERIFY(
            dialog.findChild<QLabel *>(QStringLiteral("restoreClock"))->text() != QStringLiteral("Elapsed 0:00"));
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
        auto *message = dialog.findChild<QLabel *>(QStringLiteral("restoreMessage"));
        auto *area = qobject_cast<QScrollArea *>(message->parentWidget()->parentWidget());
        QVERIFY(area);
        QVERIFY(area->height() <= 140);
        QVERIFY(message->height() > area->height());
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("restoreDone"))->isVisible());
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
        QCOMPARE(dialog.findChild<QLabel *>(QStringLiteral("restoreMessage"))->textFormat(), Qt::PlainText);
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("restoreDone"))->isVisible());
        QVERIFY(!dialog.findChild<QPushButton *>(QStringLiteral("cancelRestore"))->isVisible());
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
        auto *contents = dialog.findChild<QLabel *>(QStringLiteral("eraseContents"));
        QCOMPARE(contents->textFormat(), Qt::PlainText);
        QVERIFY(contents->text().contains(request.disk.mountPoints.front()));
        QVERIFY(contents->text().contains(request.disk.labels.front()));
        const QString plan = dialog.findChild<QLabel *>(QStringLiteral("erasePlan"))->text();
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
        auto *checkbox = dialog.findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"));
        auto *erase = dialog.findChild<QPushButton *>(QStringLiteral("confirmErase"));
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
        auto *restore = window.findChild<QPushButton *>(QStringLiteral("restoreUsb"));
        auto *status = window.findChild<QLabel *>(QStringLiteral("statusBadge"));
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
        auto *restore = window.findChild<QPushButton *>(QStringLiteral("restoreUsb"));
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
        const QString status = window.findChild<QLabel *>(QStringLiteral("statusBadge"))->text();
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
                    QTest::newRow(qPrintable(name)) << dark << windowsPlan << outcome;
                }
            }
        }
    }

    void rendersBothPlansAndThemes()
    {
        QFETCH(bool, dark);
        QFETCH(bool, windowsPlan);
        QFETCH(QString, outcome);
        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        const auto &plan = windowsPlan ? windowsRestorePlan() : linuxRestorePlan();
        RestoreDialog dialog(exampleRequest(), plan);
        dialog.show();
        const bool confirming = outcome == QStringLiteral("confirmation") || outcome == QStringLiteral("acknowledged");
        if (!confirming) {
            confirmErase(dialog);
        } else if (outcome == QStringLiteral("acknowledged")) {
            dialog.findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))->setChecked(true);
        }
        const int lastStep = confirming ? 0 : outcome == QStringLiteral("success") ? static_cast<int>(plan.size()) : 3;
        for (int step = 1; step <= lastStep; ++step) {
            dialog.setProgress(
                step, static_cast<int>(plan.size()), QStringLiteral("Preparing the drive for a fresh start…"));
        }
        if (outcome == QStringLiteral("success")) {
            dialog.finishSuccess(QStringLiteral("The disk is now one exFAT volume labelled USB at /dev/example1."));
        } else if (outcome == QStringLiteral("failed")) {
            dialog.finishFailure(QStringLiteral(
                "Could not write to USB drive 2: the drive stopped responding (error 5). "
                "Unplug the drive, plug it back in and try again. If it keeps failing, "
                "try another USB port or replace the drive."));
        } else if (outcome == QStringLiteral("cancelled")) {
            dialog.finishCancelled(QStringLiteral("The restore stopped before erasing the disk."));
        }
        QTest::qWait(80);
        QVERIFY(dialog.findChild<QPushButton *>(QStringLiteral("restoreDone"))->geometry().bottom() < dialog.height());
        QVERIFY(
            dialog.findChild<QLabel *>(QStringLiteral("restoreTitle"))->height() >=
            dialog.findChild<QLabel *>(QStringLiteral("restoreTitle"))->fontMetrics().height());
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
        QTest::newRow("light") << false;
        QTest::newRow("dark") << true;
    }

    // Saves the README's interface preview: USBRESTORE_GUI_SNAPSHOTS=docs/images.
    void rendersTheMainWindow()
    {
        QFETCH(bool, dark);
        // Earlier tests save their own selection; the preview must show first-run defaults.
        QSettings(QStringLiteral("KaroqDave"), QStringLiteral("USB Restoration Tool")).clear();
        applyTheme(*qApp, dark ? Theme::Dark : Theme::Light);
        ExampleService service;
        MainWindow window(service, dark ? Theme::Dark : Theme::Light);
        window.show();
        QTest::qWait(80);

        const QString output = qEnvironmentVariable("USBRESTORE_GUI_SNAPSHOTS");
        if (!output.isEmpty()) {
            QVERIFY(QDir().mkpath(output));
            QVERIFY(
                window.grab().save(QDir(output).filePath(QStringLiteral("app-%1.png").arg(dark ? "dark" : "light"))));
        }
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
        window.findChild<QPushButton *>(QStringLiteral("restoreUsb"))->click();
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
        cues.append(
            {1400, [&] { dialog.findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))->setChecked(true); }});
        cues.append({2400, [&] { dialog.findChild<QPushButton *>(QStringLiteral("confirmErase"))->click(); }});
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
        QCOMPARE(window.findChild<QLabel *>(QStringLiteral("version"))->text(), QStringLiteral("v%1").arg(version));

        QString title;
        QString details;
        bool hasGithub = false;
        QTimer::singleShot(0, [&] {
            auto *box = window.findChild<QMessageBox *>(QStringLiteral("aboutDialog"));
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
        window.findChild<QPushButton *>(QStringLiteral("about"))->click();

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
        auto *cancel = dialog.findChild<QPushButton *>(QStringLiteral("cancelRestore"));
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
        QVERIFY(
            dialog.findChild<QLabel *>(QStringLiteral("restoreNotice"))->text().contains(QStringLiteral("rewritten")));
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
        QVERIFY(inside(dialog.findChild<QCheckBox *>(QStringLiteral("eraseAcknowledgement"))));
        QVERIFY(inside(dialog.findChild<QPushButton *>(QStringLiteral("confirmErase"))));
        confirmErase(dialog);
        dialog.resize(dialog.minimumSize());
        QApplication::processEvents();
        QVERIFY(inside(dialog.findChild<QPushButton *>(QStringLiteral("cancelRestore"))));
        dialog.finishFailure(QStringLiteral("Example failure"));
        QApplication::processEvents();
        QVERIFY(inside(dialog.findChild<QPushButton *>(QStringLiteral("restoreDone"))));
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

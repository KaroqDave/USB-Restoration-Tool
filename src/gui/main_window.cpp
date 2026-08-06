#include "gui/main_window.h"

#include "core/safety.h"
#include "win/restore_worker.h"
#include "win/windows_util.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStyle>
#include <QStyleHints>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

#ifndef USBRESTORE_APP_VERSION
#define USBRESTORE_APP_VERSION "0.0.0-dev"
#endif

namespace usbrestore {

namespace {

constexpr auto AppAuthor = "KaroqDave";
constexpr auto AppProfileUrl = "https://github.com/KaroqDave/USB-Restoration-Tool";

// What every restore produces. Named once so the plan shown to the user and
// the label actually written cannot drift apart.
const QString RestoreVolumeLabel = QStringLiteral("USB");

QGroupBox *card(const QString &title)
{
    auto *box = new QGroupBox(title);
    box->setObjectName(QStringLiteral("card"));

    auto *shadow = new QGraphicsDropShadowEffect(box);
    shadow->setBlurRadius(24);
    shadow->setXOffset(0);
    shadow->setYOffset(4);
    shadow->setColor(QColor(15, 23, 42, 40));
    box->setGraphicsEffect(shadow);

    return box;
}

QLabel *fieldLabel(const QString &text)
{
    auto *label = new QLabel(text);
    label->setObjectName(QStringLiteral("fieldLabel"));
    return label;
}

QPushButton *styledButton(const QString &text, const char *variant)
{
    auto *button = new QPushButton(text);
    button->setProperty("variant", variant);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

void restyle(QWidget *widget, const char *variant)
{
    widget->setProperty("variant", variant);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

QString joinOrDash(const QStringList &values)
{
    return values.isEmpty() ? QStringLiteral("—") : values.join(QStringLiteral(", "));
}

QString diskListEntry(const DiskInfo &disk)
{
    const QString name = disk.name.isEmpty() ? QStringLiteral("USB disk") : disk.name;
    const QString letters = disk.driveLetters.isEmpty() ? QStringLiteral("no drive letter")
                                                        : disk.driveLetters.join(QStringLiteral(", "));
    return QStringLiteral("Disk %1  ·  %2\n%3  ·  %4  ·  %5")
        .arg(disk.number)
        .arg(name, formatByteSize(disk.size), partitionStyleName(disk.partitionStyle), letters);
}

} // namespace

MainWindow::MainWindow(Theme initialTheme, QWidget *parent) : QMainWindow(parent), m_theme(initialTheme)
{
    setWindowTitle(QStringLiteral("USB Restoration Tool"));
    setMinimumSize(880, 680);

    m_protectedDriveLetters = protectedSystemDriveLetters();

    buildUi();
    loadSettings();
    applyCurrentTheme();

    connect(&m_logger, &Logger::lineWritten, this, &MainWindow::appendLog);

    auto *refreshShortcut = new QShortcut(QKeySequence(QKeySequence::Refresh), this);
    connect(refreshShortcut, &QShortcut::activated, this, [this]() {
        if (!m_restoreRunning) {
            refreshDisks();
        }
    });

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto *hints = QApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this]() {
            if (m_theme == Theme::System) {
                applyCurrentTheme();
            }
        });
    }
#endif

    if (!m_logger.isOpen()) {
        appendLog(QStringLiteral("The log file could not be opened; this session will not be recorded."));
    }
    refreshDisks();
}

void MainWindow::buildUi()
{
    auto *scroll = new QScrollArea(this);
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *central = new QWidget;
    auto *mainLayout = new QVBoxLayout(central);
    mainLayout->setContentsMargins(28, 24, 28, 20);
    mainLayout->setSpacing(16);

    mainLayout->addLayout(buildHeaderLayout());
    mainLayout->addWidget(buildDiskSection());
    mainLayout->addWidget(buildDetailSection());
    mainLayout->addWidget(buildConfirmSection());
    mainLayout->addLayout(buildActionLayout());
    mainLayout->addWidget(buildLogSection());
    mainLayout->addWidget(buildFooterWarning());
    mainLayout->addStretch();

    scroll->setWidget(central);
    setCentralWidget(scroll);
    refreshStatusBadge();
}

QLayout *MainWindow::buildHeaderLayout()
{
    auto *header = new QGridLayout();
    header->setHorizontalSpacing(12);

    auto *title = new QLabel(QStringLiteral("USB Restoration Tool"));
    title->setObjectName(QStringLiteral("title"));
    auto *subtitle =
        new QLabel(QStringLiteral("Restore a USB drive written by an ISO writer back to one clean exFAT volume."));
    subtitle->setObjectName(QStringLiteral("subtitle"));

    auto *badge = new QLabel(QStringLiteral("ADMINISTRATOR"));
    badge->setObjectName(QStringLiteral("adminBadge"));
    badge->setToolTip(QStringLiteral("Raw disk access requires elevation, so the app always runs as Administrator."));

    auto *headerButtons = new QHBoxLayout();
    headerButtons->setSpacing(6);
    m_themeButton = styledButton(themeButtonText(m_theme), "text");
    connect(m_themeButton, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    auto *aboutButton = styledButton(QStringLiteral("About"), "text");
    connect(aboutButton, &QPushButton::clicked, this, &MainWindow::showAbout);
    headerButtons->addWidget(badge);
    headerButtons->addWidget(m_themeButton);
    headerButtons->addWidget(aboutButton);

    header->addWidget(title, 0, 0);
    header->addLayout(headerButtons, 0, 1, Qt::AlignRight | Qt::AlignTop);
    header->addWidget(subtitle, 1, 0, 1, 2);
    header->setColumnStretch(0, 1);
    return header;
}

QWidget *MainWindow::buildDiskSection()
{
    auto *box = card(QStringLiteral("USB disks"));
    auto *layout = new QGridLayout(box);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);

    m_diskList = new QListWidget;
    m_diskList->setMinimumHeight(150);
    m_diskList->setAccessibleName(QStringLiteral("Detected USB disks"));
    m_diskList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_diskList, &QListWidget::currentRowChanged, this, &MainWindow::onSelectionChanged);

    m_diskCount = new QLabel;
    m_diskCount->setObjectName(QStringLiteral("footnote"));
    m_diskCount->setWordWrap(true);

    m_refreshButton = styledButton(QStringLiteral("&Refresh"), "secondary");
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDisks);

    layout->addWidget(m_diskList, 0, 0, 1, 2);
    layout->addWidget(m_diskCount, 1, 0);
    layout->addWidget(m_refreshButton, 1, 1, Qt::AlignRight);
    layout->setColumnStretch(0, 1);
    return box;
}

QWidget *MainWindow::buildDetailSection()
{
    auto *box = card(QStringLiteral("Selected disk"));
    auto *layout = new QVBoxLayout(box);
    layout->setSpacing(8);

    m_detailTitle = new QLabel(QStringLiteral("No disk selected"));
    m_detailTitle->setObjectName(QStringLiteral("fieldLabel"));
    m_detailTitle->setWordWrap(true);

    m_detailValues = new QLabel(QStringLiteral("Only disks Windows reports on the USB bus are listed."));
    m_detailValues->setObjectName(QStringLiteral("fieldValue"));
    m_detailValues->setWordWrap(true);
    m_detailValues->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(m_detailTitle);
    layout->addWidget(m_detailValues);
    return box;
}

QWidget *MainWindow::buildConfirmSection()
{
    auto *box = card(QStringLiteral("Restore"));
    auto *layout = new QGridLayout(box);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);

    m_planLabel = new QLabel(QStringLiteral("Every partition and file on the selected disk is erased and replaced "
                                            "with one GPT partition formatted exFAT and labelled %1.")
                                 .arg(RestoreVolumeLabel));
    m_planLabel->setObjectName(QStringLiteral("footnote"));
    m_planLabel->setWordWrap(true);

    m_confirmation = new QLineEdit;
    m_confirmation->setPlaceholderText(QStringLiteral("Select a disk first"));
    m_confirmation->setAccessibleName(QStringLiteral("Restore confirmation phrase"));
    m_confirmation->setEnabled(false);
    connect(m_confirmation, &QLineEdit::textChanged, this, &MainWindow::updateRestoreState);

    m_confirmHint = new QLabel;
    m_confirmHint->setObjectName(QStringLiteral("footnote"));
    m_confirmHint->setWordWrap(true);

    m_statusLabel = new QLabel(QStringLiteral("Select a USB disk to restore."));
    m_statusLabel->setObjectName(QStringLiteral("statusBadge"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_statusLabel->setAccessibleName(QStringLiteral("Restore status"));

    m_detailLabel = new QLabel;
    m_detailLabel->setObjectName(QStringLiteral("footnote"));
    m_detailLabel->setWordWrap(true);

    layout->addWidget(m_planLabel, 0, 0, 1, 2);
    layout->addWidget(fieldLabel(QStringLiteral("Confirmation")), 1, 0);
    layout->addWidget(m_confirmation, 1, 1);
    layout->addWidget(m_confirmHint, 2, 1);
    layout->addWidget(m_statusLabel, 3, 0, 1, 2);
    layout->addWidget(m_detailLabel, 4, 0, 1, 2);
    layout->setColumnStretch(1, 1);
    return box;
}

QLayout *MainWindow::buildActionLayout()
{
    auto *layout = new QGridLayout();
    layout->setHorizontalSpacing(14);

    m_progress = new QProgressBar;
    m_progress->setRange(0, RestoreWorker::TotalSteps);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    m_progress->setFormat(QString());
    m_progress->setMinimumHeight(28);
    m_progress->setAccessibleName(QStringLiteral("Restore progress"));

    m_restoreButton = styledButton(QStringLiteral("Restore USB"), "danger");
    m_restoreButton->setMinimumHeight(40);
    m_restoreButton->setEnabled(false);
    m_restoreButton->setAccessibleName(QStringLiteral("Restore the selected USB disk"));
    connect(m_restoreButton, &QPushButton::clicked, this, [this]() {
        if (m_restoreRunning) {
            cancelRestore();
        } else {
            startRestore();
        }
    });

    layout->addWidget(m_progress, 0, 0);
    layout->addWidget(m_restoreButton, 0, 1);
    layout->setColumnStretch(0, 1);
    return layout;
}

QWidget *MainWindow::buildLogSection()
{
    auto *box = card(QStringLiteral("Activity"));
    auto *layout = new QVBoxLayout(box);
    layout->setSpacing(10);

    m_log = new QTextEdit;
    m_log->setObjectName(QStringLiteral("activityLog"));
    m_log->setReadOnly(true);
    m_log->setMinimumHeight(140);
    m_log->setAccessibleName(QStringLiteral("Activity log"));

    auto *openLogButton = styledButton(QStringLiteral("Open log file"), "secondary");
    connect(openLogButton, &QPushButton::clicked, this, &MainWindow::openLogFile);

    layout->addWidget(m_log);
    layout->addWidget(openLogButton, 0, Qt::AlignRight);
    return box;
}

QWidget *MainWindow::buildFooterWarning()
{
    auto *warning = new QLabel(QStringLiteral(
        "Restoring erases every partition and file on the selected disk. Disks that Windows reports as boot, "
        "system, offline, read-only, or on any bus other than USB are never offered."));
    warning->setObjectName(QStringLiteral("footnote"));
    warning->setWordWrap(true);
    return warning;
}

void MainWindow::loadSettings()
{
    m_settings = loadAppSettings();
    m_settings.theme = m_theme;
    if (!m_settings.geometry.isEmpty()) {
        restoreGeometry(m_settings.geometry);
    }
}

void MainWindow::saveSettings()
{
    m_settings.theme = m_theme;
    m_settings.geometry = saveGeometry();
    saveAppSettings(m_settings);
}

void MainWindow::refreshDisks()
{
    if (m_restoreRunning) {
        return;
    }

    // A rescan invalidates whatever the user had typed: the phrase names a disk
    // number, and the disk behind that number may not be the same one now.
    const QSignalBlocker blocker(*m_diskList);
    QString error;
    m_disks = m_enumerator.listUsbDisks(&error);
    renderDisks();
    onSelectionChanged();

    // Reported after the selection has been rebuilt, so the failure is the last
    // word on screen rather than being overwritten by the empty-list status.
    if (!error.isEmpty()) {
        appendLog(QStringLiteral("Scan failed: %1").arg(error));
        setStatus(StatusKind::Error, QStringLiteral("Could not scan for USB disks."), error);
    }
}

void MainWindow::renderDisks()
{
    m_diskList->clear();
    for (const DiskInfo &disk : m_disks) {
        auto *item = new QListWidgetItem(diskListEntry(disk), m_diskList);
        QString reason;
        if (!isSafeRestoreTarget(disk, &reason, m_protectedDriveLetters)) {
            item->setToolTip(reason);
        }
    }

    if (m_disks.isEmpty()) {
        m_diskCount->setText(QStringLiteral("No USB disks detected. Plug the drive in and select Refresh."));
    } else {
        m_diskCount->setText(m_disks.size() == 1 ? QStringLiteral("1 USB disk detected.")
                                                 : QStringLiteral("%1 USB disks detected.").arg(m_disks.size()));
    }

    if (!m_disks.isEmpty()) {
        m_diskList->setCurrentRow(0);
    }
}

void MainWindow::onSelectionChanged()
{
    m_confirmation->clear();
    renderSelectedDisk();
    updateRestoreState();
}

const DiskInfo *MainWindow::selectedDisk() const
{
    const int row = m_diskList->currentRow();
    if (row < 0 || row >= m_disks.size()) {
        return nullptr;
    }
    return &m_disks.at(row);
}

void MainWindow::renderSelectedDisk()
{
    const DiskInfo *disk = selectedDisk();
    if (!disk) {
        m_detailTitle->setText(QStringLiteral("No disk selected"));
        m_detailValues->setText(QStringLiteral("Only disks Windows reports on the USB bus are listed."));
        m_confirmation->setPlaceholderText(QStringLiteral("Select a disk first"));
        return;
    }

    m_detailTitle->setText(describeDisk(*disk));
    m_detailValues->setText(QStringLiteral("Bus: %1\n"
                                           "Current layout: %2\n"
                                           "Drive letters: %3\n"
                                           "Volume labels: %4\n"
                                           "Sector size: %5 bytes\n"
                                           "Health: %6")
                                .arg(busTypeName(disk->busType),
                                     partitionStyleName(disk->partitionStyle),
                                     joinOrDash(disk->driveLetters),
                                     joinOrDash(disk->labels))
                                .arg(disk->sectorSize)
                                .arg(healthStatusName(disk->health)));
    m_confirmation->setPlaceholderText(QStringLiteral("Type %1").arg(confirmationPhrase(disk->number)));
}

void MainWindow::updateRestoreState()
{
    if (m_restoreRunning) {
        return;
    }

    const DiskInfo *disk = selectedDisk();
    if (!disk) {
        m_confirmation->setEnabled(false);
        m_confirmHint->clear();
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Info,
                  QStringLiteral("Select a USB disk to restore."),
                  m_disks.isEmpty() ? QStringLiteral("Nothing to restore until a USB disk is plugged in.")
                                    : QString());
        return;
    }

    QString reason;
    if (!isSafeRestoreTarget(*disk, &reason, m_protectedDriveLetters)) {
        m_confirmation->setEnabled(false);
        m_confirmation->clear();
        m_confirmHint->clear();
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Blocked, QStringLiteral("This disk will not be restored."), reason);
        return;
    }

    m_confirmation->setEnabled(true);
    const QString phrase = confirmationPhrase(disk->number);
    if (m_confirmation->text() != phrase) {
        m_confirmHint->setText(QStringLiteral("Type %1 exactly to enable the restore.").arg(phrase));
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Ready,
                  QStringLiteral("Ready to restore %1.").arg(describeDisk(*disk)),
                  QStringLiteral("The restore stays disabled until the confirmation phrase is typed."));
        return;
    }

    m_confirmHint->clear();
    m_restoreButton->setEnabled(true);

    const QString warning = largeRestoreTargetWarning(*disk);
    if (!warning.isEmpty()) {
        setStatus(StatusKind::Warning, QStringLiteral("Confirmed — but check this is the right disk."), warning);
        return;
    }
    setStatus(StatusKind::Ready,
              QStringLiteral("Confirmed. Selecting Restore USB erases disk %1.").arg(disk->number),
              QStringLiteral("Contents: %1").arg(joinOrDash(disk->labels)));
}

void MainWindow::startRestore()
{
    const DiskInfo *selected = selectedDisk();
    if (!selected) {
        return;
    }

    QString reason;
    if (!isSafeRestoreTarget(*selected, &reason, m_protectedDriveLetters)) {
        QMessageBox::critical(this, QStringLiteral("Restore refused"), reason);
        updateRestoreState();
        return;
    }
    if (m_confirmation->text() != confirmationPhrase(selected->number)) {
        updateRestoreState();
        return;
    }

    const DiskInfo disk = *selected;

    QString summary = QStringLiteral("%1\n\nDrive letters: %2\nVolume labels: %3\n\n"
                                     "Every partition and file on this disk will be erased and replaced with one "
                                     "GPT partition formatted exFAT and labelled %4.")
                          .arg(describeDisk(disk),
                               joinOrDash(disk.driveLetters),
                               joinOrDash(disk.labels),
                               RestoreVolumeLabel);
    const QString largeWarning = largeRestoreTargetWarning(disk);
    if (!largeWarning.isEmpty()) {
        summary += QStringLiteral("\n\n%1").arg(largeWarning);
    }

    QMessageBox confirm(this);
    confirm.setIcon(QMessageBox::Warning);
    confirm.setWindowTitle(QStringLiteral("Erase this USB disk?"));
    confirm.setText(QStringLiteral("This cannot be undone."));
    confirm.setInformativeText(summary);
    QPushButton *eraseButton = confirm.addButton(QStringLiteral("Erase and restore"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(cancelButton);
    confirm.exec();
    if (confirm.clickedButton() != eraseButton) {
        return;
    }

    m_logger.log(QStringLiteral("Restore requested for %1").arg(describeDisk(disk)));
    m_logger.logFileOnly(QStringLiteral("Target details — bus %1, sector size %2, letters [%3], labels [%4]")
                             .arg(busTypeName(disk.busType))
                             .arg(disk.sectorSize)
                             .arg(disk.driveLetters.join(QStringLiteral(", ")),
                                  disk.labels.join(QStringLiteral(", "))));

    setRunning(true);
    setStatus(StatusKind::Running, QStringLiteral("Restoring disk %1...").arg(disk.number));

    auto *thread = new QThread(this);
    auto *worker = new RestoreWorker(disk, m_protectedDriveLetters, RestoreVolumeLabel);
    worker->moveToThread(thread);
    m_restoreThread = thread;
    m_restoreWorker = worker;

    connect(thread, &QThread::started, worker, &RestoreWorker::run);
    connect(worker, &RestoreWorker::progress, this, &MainWindow::onRestoreProgress);
    connect(worker, &RestoreWorker::logMessage, &m_logger, &Logger::log, Qt::QueuedConnection);
    connect(worker, &RestoreWorker::logFileOnly, &m_logger, &Logger::logFileOnly, Qt::QueuedConnection);
    connect(worker, &RestoreWorker::failed, this, &MainWindow::onRestoreFailed);
    connect(worker, &RestoreWorker::cancelled, this, &MainWindow::onRestoreCancelled);
    connect(worker, &RestoreWorker::finished, this, &MainWindow::onRestoreFinished);
    connect(worker, &RestoreWorker::failed, thread, &QThread::quit);
    connect(worker, &RestoreWorker::cancelled, thread, &QThread::quit);
    connect(worker, &RestoreWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::cancelRestore()
{
    if (!m_restoreRunning || !m_restoreWorker) {
        return;
    }

    m_restoreWorker->requestCancel();
    m_restoreButton->setEnabled(false);
    setStatus(StatusKind::Running,
              QStringLiteral("Stopping..."),
              QStringLiteral("The restore stops at the next checkpoint. Once the disk has been written to there is "
                             "nothing left to stop, and it runs to completion."));
}

void MainWindow::onRestoreProgress(int step, int totalSteps, const QString &message)
{
    m_progress->setRange(0, totalSteps);
    m_progress->setValue(step);
    m_progress->setFormat(QStringLiteral("%1 / %2 — %3").arg(step).arg(totalSteps).arg(message));
    m_progress->setAccessibleDescription(message);
    setStatus(StatusKind::Running, message, QStringLiteral("Step %1 of %2.").arg(step).arg(totalSteps));
}

// The three endings share a shape: stop, rescan — because the disk on screen
// has just changed underneath the list — and only then set the status, so the
// rescan's own "ready to restore" verdict does not overwrite the outcome.
void MainWindow::onRestoreFailed(const QString &message)
{
    setRunning(false);
    refreshDisks();
    setStatus(StatusKind::Error, QStringLiteral("The restore failed."), message);
    QMessageBox::critical(this,
                          QStringLiteral("Restore failed"),
                          QStringLiteral("%1\n\nLog file:\n%2").arg(message, m_logger.path()));
}

void MainWindow::onRestoreCancelled()
{
    setRunning(false);
    m_logger.log(QStringLiteral("Restore cancelled before any change was made"));
    refreshDisks();
    setStatus(StatusKind::Info, QStringLiteral("Restore cancelled."), QStringLiteral("The disk was not changed."));
}

void MainWindow::onRestoreFinished(const QString &driveRoot)
{
    setRunning(false);
    refreshDisks();
    m_progress->setValue(m_progress->maximum());
    m_progress->setFormat(QStringLiteral("Restore complete"));
    setStatus(StatusKind::Success,
              QStringLiteral("Restore complete."),
              QStringLiteral("The disk is now one exFAT volume labelled %1 at %2.").arg(RestoreVolumeLabel, driveRoot));
    QMessageBox::information(this,
                             QStringLiteral("Restore complete"),
                             QStringLiteral("The USB disk was restored as %1.").arg(driveRoot));
}

void MainWindow::setRunning(bool running)
{
    m_restoreRunning = running;

    m_diskList->setEnabled(!running);
    m_refreshButton->setEnabled(!running);
    m_confirmation->setEnabled(!running && selectedDisk() != nullptr);

    m_restoreButton->setEnabled(true);
    m_restoreButton->setText(running ? QStringLiteral("Cancel") : QStringLiteral("Restore USB"));
    restyle(m_restoreButton, running ? "secondary" : "danger");

    if (!running) {
        m_progress->setFormat(QString());
        m_progress->setValue(0);
        m_confirmation->clear();
        m_restoreWorker = nullptr;
        updateRestoreState();
    }
}

void MainWindow::setStatus(StatusKind status, const QString &message, const QString &detail)
{
    m_status = status;
    m_statusLabel->setText(statusBadgePrefix(status) + message);
    m_detailLabel->setText(detail);
    refreshStatusBadge();
}

void MainWindow::refreshStatusBadge()
{
    const Palette &palette = paletteFor(resolveColorScheme(m_theme));
    const QColor bg = statusBadgeBackground(m_status, palette);
    const QColor fg = statusBadgeText(m_status, palette);

    m_statusLabel->setStyleSheet(QStringLiteral("QLabel#statusBadge { background: rgba(%1, %2, %3, %4); color: %5; "
                                                "border-radius: 8px; padding: 8px 12px; font-weight: 700; }")
                                     .arg(bg.red())
                                     .arg(bg.green())
                                     .arg(bg.blue())
                                     .arg(bg.alpha())
                                     .arg(fg.name(QColor::HexRgb)));
}

void MainWindow::appendLog(const QString &message)
{
    m_log->append(message);
}

void MainWindow::toggleTheme()
{
    switch (m_theme) {
    case Theme::System:
        m_theme = Theme::Light;
        break;
    case Theme::Light:
        m_theme = Theme::Dark;
        break;
    case Theme::Dark:
        m_theme = Theme::System;
        break;
    }
    applyCurrentTheme();
    saveSettings();
}

void MainWindow::applyCurrentTheme()
{
    usbrestore::applyTheme(*qApp, m_theme);
    if (m_themeButton) {
        m_themeButton->setText(themeButtonText(m_theme));
    }
    refreshStatusBadge();
}

void MainWindow::showAbout()
{
    QMessageBox about(this);
    about.setWindowTitle(QStringLiteral("About USB Restoration Tool"));
    about.setText(QStringLiteral("USB Restoration Tool %1").arg(QString::fromLatin1(USBRESTORE_APP_VERSION)));
    about.setInformativeText(QStringLiteral("Created by %1\n\n"
                                            "Restores a USB drive written by an ISO writer, Linux installer, or boot "
                                            "media tool back to one GPT partition formatted exFAT.\n\n"
                                            "Only USB disks are listed, and boot, system, offline, and read-only "
                                            "disks are refused.")
                                 .arg(QString::fromLatin1(AppAuthor)));
    about.setStandardButtons(QMessageBox::Ok);
    QPushButton *githubButton = about.addButton(QStringLiteral("Open GitHub"), QMessageBox::ActionRole);
    about.exec();
    if (about.clickedButton() == githubButton) {
        openAsInvokingUser(QString::fromLatin1(AppProfileUrl));
    }
}

void MainWindow::openLogFile()
{
    const QString path = m_logger.path();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this,
                                 QStringLiteral("No log file"),
                                 QStringLiteral("No log file has been written yet."));
        return;
    }

    if (!openAsInvokingUser(QDir::toNativeSeparators(path))) {
        QMessageBox::information(this,
                                 QStringLiteral("Log file"),
                                 QStringLiteral("The log file could not be opened. It is at:\n\n%1").arg(path));
    }
}

bool MainWindow::waitForRestoreThread(int timeoutMs)
{
    if (!m_restoreThread || !m_restoreThread->isRunning()) {
        return true;
    }

    QElapsedTimer timer;
    timer.start();
    while (m_restoreThread && m_restoreThread->isRunning() && timer.elapsed() < timeoutMs) {
        QApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
    return !m_restoreThread || !m_restoreThread->isRunning();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_restoreRunning && m_restoreThread && m_restoreThread->isRunning()) {
        // Killing the process partway through a partition rewrite leaves a disk
        // that neither Windows nor the previous ISO can read, so closing waits
        // rather than offering to abandon the work.
        QMessageBox::information(this,
                                 QStringLiteral("Restore in progress"),
                                 QStringLiteral("A restore is still running. Wait for it to finish before closing; "
                                                "stopping now would leave the disk unusable."));
        event->ignore();
        return;
    }

    waitForRestoreThread(5000);
    saveSettings();
    event->accept();
}

} // namespace usbrestore

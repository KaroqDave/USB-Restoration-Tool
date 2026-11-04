#include "gui/main_window.h"
#include "core/safety.h"
#include "gui/restore_dialog.h"
#include "platform/restore_worker.h"
#include "platform/translations.h"

#ifdef Q_OS_WIN
#include "win/windows_util.h"
#endif

#include <QApplication>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QEvent>
#include <QEventLoop>
#include <QFileInfo>
#include <QFrame>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QStyleHints>
#include <QTextEdit>
#include <QThread>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#ifndef USBRESTORE_APP_VERSION
#define USBRESTORE_APP_VERSION "0.0.0-dev"
#endif

namespace usbrestore {

namespace {

constexpr auto AppAuthor = "KaroqDave";
constexpr auto AppProfileUrl = "https://github.com/KaroqDave/USB-Restoration-Tool";

// Named once so the plan shown to the user and the label written cannot drift apart.
const QString RestoreVolumeLabel = QStringLiteral("USB");

// Small enough to sit comfortably on a laptop screen.
constexpr int MinimumWindowWidth = 780;
constexpr int MinimumWindowHeight = 560;

// Silence before the dialog reports it, and before the stall is explained. A stick
// can sit in uninterruptible sleep for minutes and recover (4.5 minutes seen on hardware).
constexpr qint64 StallCountSeconds = 15;
constexpr qint64 StallExplainSeconds = 60;

QGroupBox *card()
{
    auto *box = new QGroupBox;
    box->setObjectName(QStringLiteral("card"));

    return box;
}

QLabel *fieldLabel()
{
    auto *label = new QLabel;
    label->setObjectName(QStringLiteral("fieldLabel"));
    return label;
}

QLabel *footnoteLabel()
{
    auto *label = new QLabel;
    label->setObjectName(QStringLiteral("footnote"));
    label->setWordWrap(true);
    return label;
}

QPushButton *styledButton(const char *variant)
{
    auto *button = new QPushButton;
    button->setProperty("variant", variant);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}

QString joinOrDash(const QStringList &values)
{
    return values.isEmpty() ? QStringLiteral("—") : values.join(QStringLiteral(", "));
}

void styleComboPopup(QComboBox *combo)
{
    // The native popup ignores the stylesheet's item styling.
    auto *popupView = new QListView(combo);
    popupView->setFrameShape(QFrame::NoFrame);
    combo->setView(popupView);
    for (int i = 0; i < combo->count(); ++i) {
        combo->setItemData(i, QSize(0, 32), Qt::SizeHintRole);
    }
}

QString diskListEntry(const DiskInfo &disk)
{
    const QString name =
        disk.name.isEmpty() ? QCoreApplication::translate("usbrestore::MainWindow", "USB disk") : disk.name;
    const QString mounts = disk.mountPoints.isEmpty()
                               ? QCoreApplication::translate("usbrestore::MainWindow", "not mounted")
                               : disk.mountPoints.join(QStringLiteral(", "));
    return QStringLiteral("%1  ·  %2\n%3  ·  %4  ·  %5")
        .arg(
            disk.displayId.isEmpty() ? disk.deviceId : disk.displayId,
            name,
            formatByteSize(disk.size, TextMode::Display),
            partitionStyleName(disk.partitionStyle, TextMode::Display),
            mounts);
}

// Keeps elevated privileges out of whatever handles the target: on Windows explorer
// opens it as the logged-in user.
void openExternally(const QString &target, bool isLocalFile)
{
#ifdef Q_OS_WIN
    if (openAsInvokingUser(isLocalFile ? QDir::toNativeSeparators(target) : target)) {
        return;
    }
#endif
    QDesktopServices::openUrl(isLocalFile ? QUrl::fromLocalFile(target) : QUrl(target));
}

} // namespace

MainWindow::MainWindow(DiskService &service, Theme initialTheme, Translations *translations, QWidget *parent)
    : QMainWindow(parent), m_service(service), m_translations(translations), m_theme(initialTheme)
{
    setWindowTitle(QStringLiteral("USB Restoration Tool"));
    setMinimumSize(MinimumWindowWidth, MinimumWindowHeight);
    resize(960, 680);

    m_guard = m_service.restoreGuard();

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
    } else if (!m_logger.isPortable()) {
        appendLog(QStringLiteral("The application folder is not writable, so the log is being kept at %1")
                      .arg(m_logger.path()));
    }
    applyTranslatedText();
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
    mainLayout->setContentsMargins(28, 20, 28, 20);
    mainLayout->setSpacing(16);

    mainLayout->addLayout(buildHeaderLayout());

    auto *workspace = new QGridLayout;
    workspace->setHorizontalSpacing(18);
    workspace->setVerticalSpacing(14);
    workspace->addWidget(buildDiskSection(), 0, 0);
    workspace->addWidget(buildDetailSection(), 1, 0);
    workspace->addWidget(buildRestoreSection(), 0, 1, 2, 1);
    workspace->setColumnStretch(0, 5);
    workspace->setColumnStretch(1, 4);
    workspace->setRowStretch(0, 1);
    mainLayout->addLayout(workspace);
    mainLayout->addWidget(buildFooterWarning());
    mainLayout->addStretch();

    scroll->setWidget(central);
    setCentralWidget(scroll);

    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(1000);
    connect(m_stallTimer, &QTimer::timeout, this, &MainWindow::onStallTick);

    // Outside the layout: it fills the activity dialog and logs whether or not that is open.
    m_log = new QTextEdit(this);
    m_log->hide();
    m_log->setObjectName(QStringLiteral("activityLog"));
    m_log->setReadOnly(true);
    m_log->setMinimumSize(560, 320);

    refreshStatusBadge();
}

QLayout *MainWindow::buildHeaderLayout()
{
    auto *header = new QGridLayout();
    header->setHorizontalSpacing(16);

    auto *mark = new QLabel;
    mark->setPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(QSize(56, 56), devicePixelRatioF()));
    mark->setFixedSize(56, 56);
    auto *brand = new QHBoxLayout;
    brand->setSpacing(12);
    auto *wordmark = new QVBoxLayout;
    wordmark->setSpacing(2);
    auto *title = new QLabel(QStringLiteral("USB Restoration Tool"));
    title->setObjectName(QStringLiteral("title"));
    m_subtitle = new QLabel;
    m_subtitle->setObjectName(QStringLiteral("subtitle"));
    wordmark->addWidget(title);
    wordmark->addWidget(m_subtitle);
    brand->addWidget(mark);
    brand->addLayout(wordmark);
    brand->addStretch();

    m_privilegeBadge = new QLabel;
    m_privilegeBadge->setObjectName(QStringLiteral("adminBadge"));

    auto *headerButtons = new QHBoxLayout();
    headerButtons->setSpacing(4);
    m_activityButton = styledButton("text");
    m_activityButton->setObjectName(QStringLiteral("activity"));
    connect(m_activityButton, &QPushButton::clicked, this, &MainWindow::showActivity);
    m_languageButton = styledButton("text");
    m_languageButton->setObjectName(QStringLiteral("language"));
    connect(m_languageButton, &QPushButton::clicked, this, &MainWindow::cycleLanguage);
    m_themeButton = styledButton("text");
    m_themeButton->setObjectName(QStringLiteral("theme"));
    connect(m_themeButton, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    m_aboutButton = styledButton("text");
    m_aboutButton->setObjectName(QStringLiteral("about"));
    connect(m_aboutButton, &QPushButton::clicked, this, &MainWindow::showAbout);
    headerButtons->addWidget(m_activityButton);
    headerButtons->addWidget(m_languageButton);
    headerButtons->addWidget(m_themeButton);
    headerButtons->addWidget(m_aboutButton);

    auto *utilities = new QVBoxLayout;
    utilities->setSpacing(6);
    auto *metadata = new QHBoxLayout;
    auto *version = new QLabel(QStringLiteral("v%1").arg(QString::fromLatin1(USBRESTORE_APP_VERSION)));
    version->setObjectName(QStringLiteral("version"));
    metadata->addStretch();
    metadata->addWidget(version);
    metadata->addSpacing(8);
    metadata->addWidget(m_privilegeBadge);
    utilities->addLayout(metadata);
    utilities->addLayout(headerButtons);
    header->addLayout(brand, 0, 0);
    header->addLayout(utilities, 0, 1, Qt::AlignRight);
    header->setColumnStretch(0, 1);
    return header;
}

QWidget *MainWindow::buildDiskSection()
{
    m_diskCard = card();
    auto *layout = new QGridLayout(m_diskCard);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);

    m_diskList = new QListWidget;
    m_diskList->setMinimumHeight(156);
    m_diskList->setSelectionMode(QAbstractItemView::SingleSelection);
    connect(m_diskList, &QListWidget::currentRowChanged, this, &MainWindow::onSelectionChanged);

    m_diskCount = footnoteLabel();

    m_refreshButton = styledButton("secondary");
    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDisks);

    layout->addWidget(m_diskList, 0, 0, 1, 2);
    layout->addWidget(m_diskCount, 1, 0);
    layout->addWidget(m_refreshButton, 1, 1, Qt::AlignRight);
    layout->setColumnStretch(0, 1);
    return m_diskCard;
}

QWidget *MainWindow::buildDetailSection()
{
    m_detailCard = card();
    auto *layout = new QVBoxLayout(m_detailCard);
    layout->setSpacing(6);

    m_detailTitle = new QLabel;
    m_detailTitle->setObjectName(QStringLiteral("detailTitle"));
    m_detailTitle->setWordWrap(true);

    m_detailValues = new QLabel;
    m_detailValues->setObjectName(QStringLiteral("fieldValue"));
    m_detailValues->setWordWrap(true);
    m_detailValues->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(m_detailTitle);
    layout->addWidget(m_detailValues);
    return m_detailCard;
}

QWidget *MainWindow::buildRestoreSection()
{
    m_restoreCard = card();
    auto *layout = new QGridLayout(m_restoreCard);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);

    m_styleCombo = new QComboBox;
    m_styleCombo->addItem(QString(), static_cast<int>(PartitionStyle::Gpt));
    m_styleCombo->addItem(QString(), static_cast<int>(PartitionStyle::Mbr));
    m_styleCombo->setCursor(Qt::PointingHandCursor);
    styleComboPopup(m_styleCombo);
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateRestoreState);

    m_fileSystemCombo = new QComboBox;
    m_fileSystemCombo->setCursor(Qt::PointingHandCursor);
    for (const FileSystemType type : m_service.supportedFileSystems()) {
        m_fileSystemCombo->addItem(QString(), static_cast<int>(type));
    }
    styleComboPopup(m_fileSystemCombo);
    connect(m_fileSystemCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateRestoreState);

    m_planLabel = footnoteLabel();

    m_statusLabel = new QLabel;
    m_statusLabel->setObjectName(QStringLiteral("statusBadge"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);

    m_detailLabel = footnoteLabel();

    m_restoreButton = styledButton("danger");
    m_restoreButton->setObjectName(QStringLiteral("restoreUsb"));
    m_restoreButton->setMinimumHeight(40);
    m_restoreButton->setEnabled(false);
    connect(m_restoreButton, &QPushButton::clicked, this, &MainWindow::startRestore);

    m_partitionLabel = fieldLabel();
    m_filesystemLabel = fieldLabel();
    layout->addWidget(m_partitionLabel, 0, 0);
    layout->addWidget(m_styleCombo, 1, 0);
    layout->addWidget(m_filesystemLabel, 2, 0);
    layout->addWidget(m_fileSystemCombo, 3, 0);
    layout->addWidget(m_planLabel, 4, 0);
    layout->setRowMinimumHeight(5, 6);
    layout->addWidget(m_statusLabel, 6, 0);
    layout->addWidget(m_detailLabel, 7, 0);
    layout->setRowStretch(8, 1);
    layout->addWidget(m_restoreButton, 9, 0);
    return m_restoreCard;
}

QWidget *MainWindow::buildFooterWarning()
{
    auto *banner = new QFrame;
    banner->setObjectName(QStringLiteral("warningBanner"));
    auto *layout = new QVBoxLayout(banner);
    layout->setContentsMargins(16, 12, 16, 12);
    layout->setSpacing(3);
    m_warningTitle = new QLabel;
    m_warningTitle->setObjectName(QStringLiteral("warningTitle"));
    m_warningTitle->setWordWrap(true);
    m_warningDetail = footnoteLabel();
    layout->addWidget(m_warningTitle);
    layout->addWidget(m_warningDetail);
    return banner;
}

QDialog *MainWindow::activityDialog()
{
    if (m_activityDialog) {
        return m_activityDialog;
    }

    auto *dialog = new QDialog(this);
    dialog->setModal(false);
    dialog->resize(680, 420);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    m_activityCaption = footnoteLabel();
    layout->addWidget(m_activityCaption);
    layout->addWidget(m_log, 1);
    m_log->show();

    auto *buttons = new QHBoxLayout();
    m_openLogButton = styledButton("secondary");
    connect(m_openLogButton, &QPushButton::clicked, this, &MainWindow::openLogFile);
    m_activityCloseButton = styledButton("secondary");
    connect(m_activityCloseButton, &QPushButton::clicked, dialog, &QDialog::hide);
    buttons->addWidget(m_openLogButton);
    buttons->addStretch();
    buttons->addWidget(m_activityCloseButton);
    layout->addLayout(buttons);

    m_activityDialog = dialog;
    applyActivityDialogText();
    return dialog;
}

void MainWindow::showActivity()
{
    QDialog *dialog = activityDialog();
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void MainWindow::loadSettings()
{
    m_settings = loadAppSettings();
    if (!m_settings.geometry.isEmpty()) {
        restoreGeometry(m_settings.geometry);
    }
    if (m_styleCombo) {
        const int index = m_styleCombo->findData(static_cast<int>(m_settings.partitionStyle));
        if (index >= 0) {
            m_styleCombo->setCurrentIndex(index);
        }
    }
    if (m_fileSystemCombo) {
        const int index = m_fileSystemCombo->findData(static_cast<int>(m_settings.fileSystem));
        if (index >= 0) {
            m_fileSystemCombo->setCurrentIndex(index);
        }
    }
}

void MainWindow::saveSettings()
{
    m_settings.theme = m_theme;
    m_settings.geometry = saveGeometry();
    m_settings.partitionStyle = selectedPartitionStyle();
    m_settings.fileSystem = selectedFileSystem();
    saveAppSettings(m_settings);
}

PartitionStyle MainWindow::selectedPartitionStyle() const
{
    if (!m_styleCombo) {
        return PartitionStyle::Gpt;
    }
    const int value = m_styleCombo->currentData().toInt();
    return value == static_cast<int>(PartitionStyle::Mbr) ? PartitionStyle::Mbr : PartitionStyle::Gpt;
}

FileSystemType MainWindow::selectedFileSystem() const
{
    if (!m_fileSystemCombo) {
        return FileSystemType::ExFat;
    }
    const auto type = static_cast<FileSystemType>(m_fileSystemCombo->currentData().toInt());
    return isKnownFileSystem(type) ? type : FileSystemType::ExFat;
}

void MainWindow::refreshDisks()
{
    if (m_restoreRunning) {
        return;
    }

    const QSignalBlocker blocker(*m_diskList);
    m_guard = m_service.restoreGuard();
    m_scanError = {};
    m_disks = m_service.listUsbDisks(&m_scanError);
    renderDisks();
    onSelectionChanged();

    if (!m_scanError.english.isEmpty()) {
        m_logger.log(QStringLiteral("Scan failed: %1").arg(m_scanError.english));
    }
}

void MainWindow::renderDisks()
{
    m_diskList->clear();
    for (const DiskInfo &disk : m_disks) {
        auto *item = new QListWidgetItem(diskListEntry(disk), m_diskList);
        QString reason;
        if (!isSafeRestoreTarget(disk, m_guard, &reason, TextMode::Display)) {
            item->setToolTip(reason);
        }
    }

    if (m_disks.isEmpty()) {
        m_diskCount->setText(tr("No USB disks detected. Plug the drive in and select Refresh."));
    } else {
        m_diskCount->setText(
            m_disks.size() == 1 ? tr("1 USB disk detected.") : tr("%1 USB disks detected.").arg(m_disks.size()));
    }

    if (!m_disks.isEmpty()) {
        m_diskList->setCurrentRow(0);
    }
}

void MainWindow::onSelectionChanged()
{
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
        m_detailTitle->setText(tr("No disk selected"));
        m_detailValues->setText(tr("Only disks reported on the USB bus are listed."));
        return;
    }

    m_detailTitle->setText(describeDisk(*disk, TextMode::Display));
    m_detailValues->setText(tr("Bus: %1    Current layout: %2    Sector size: %3 bytes\n"
                               "Filesystems: %4\n"
                               "Mounted at: %5\n"
                               "Volume labels: %6")
                                .arg(
                                    busTypeName(disk->busType, TextMode::Display),
                                    partitionStyleName(disk->partitionStyle, TextMode::Display),
                                    QString::number(disk->sectorSize),
                                    joinOrDash(disk->fileSystems),
                                    joinOrDash(disk->mountPoints),
                                    joinOrDash(disk->labels)));
}

void MainWindow::updateRestoreState()
{
    if (m_restoreRunning) {
        return;
    }

    const QString style = partitionStyleLabel(selectedPartitionStyle());
    const QString fileSystem = fileSystemTypeName(selectedFileSystem());
    m_planLabel->setText(tr("Erases all contents. Creates one %1 partition formatted %2, labelled %3.")
                             .arg(style, fileSystem, RestoreVolumeLabel));

    if (!m_scanError.english.isEmpty()) {
        m_styleCombo->setEnabled(false);
        m_fileSystemCombo->setEnabled(false);
        m_restoreButton->setEnabled(false);
        const QString detail = m_scanError.translated();
        setStatus(
            StatusKind::Error, tr("Could not scan for USB disks."), detail.isEmpty() ? m_scanError.english : detail);
        return;
    }

    const DiskInfo *disk = selectedDisk();
    if (!disk) {
        m_styleCombo->setEnabled(false);
        m_fileSystemCombo->setEnabled(false);
        m_restoreButton->setEnabled(false);
        setStatus(
            StatusKind::Info,
            tr("Select a USB disk to restore."),
            m_disks.isEmpty() ? tr("Nothing to restore until a USB disk is plugged in.") : QString());
        return;
    }

    m_styleCombo->setEnabled(true);
    m_fileSystemCombo->setEnabled(true);

    QString reason;
    if (!isSafeRestoreTarget(*disk, m_guard, &reason, TextMode::Display)) {
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Blocked, tr("This disk will not be restored."), reason);
        return;
    }

    if (!canCreatePartitionLayout(selectedPartitionStyle(), *disk, &reason, TextMode::Display) ||
        !m_service.canFormatFileSystem(selectedFileSystem(), *disk, nullptr, &reason)) {
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Blocked, tr("This restore configuration cannot be used on this disk."), reason);
        return;
    }

    m_restoreButton->setEnabled(true);

    const QString warning = largeRestoreTargetWarning(*disk, TextMode::Display);
    if (!warning.isEmpty()) {
        setStatus(StatusKind::Warning, tr("Check this is the right disk."), warning);
        return;
    }
    setStatus(
        StatusKind::Ready,
        tr("Ready to restore %1 as %2 %3.").arg(describeDisk(*disk, TextMode::Display), style, fileSystem),
        tr("Contents: %1").arg(joinOrDash(disk->labels)));
}

void MainWindow::startRestore()
{
    if (m_restoreRunning) {
        return;
    }
    if (m_restoreDialog && m_restoreDialog->isVisible()) {
        m_restoreDialog->raise();
        m_restoreDialog->activateWindow();
        return;
    }
    const DiskInfo *selected = selectedDisk();
    if (!selected) {
        return;
    }

    QString reason;
    if (!isSafeRestoreTarget(*selected, m_guard, &reason, TextMode::Display)) {
        QMessageBox::critical(this, tr("Restore refused"), reason);
        updateRestoreState();
        return;
    }

    const DiskInfo disk = *selected;
    const PartitionStyle style = selectedPartitionStyle();
    const FileSystemType fileSystem = selectedFileSystem();
    if (!canCreatePartitionLayout(style, disk, &reason, TextMode::Display) ||
        !m_service.canFormatFileSystem(fileSystem, disk, nullptr, &reason)) {
        QMessageBox::critical(this, tr("Restore refused"), reason);
        updateRestoreState();
        return;
    }

    RestoreRequest request;
    request.disk = disk;
    request.style = style;
    request.fileSystem = fileSystem;
    request.volumeLabel = RestoreVolumeLabel;
    request.guard = m_guard;
    request.displayLanguage = m_translations ? m_translations->language() : Language::English;

    delete m_restoreDialog.data();
    m_restoreDialog = new RestoreDialog(request, m_service.restorePlan(), this);
    connect(m_restoreDialog, &RestoreDialog::restoreConfirmed, this, [this, request]() { beginRestore(request); });
    connect(m_restoreDialog, &RestoreDialog::cancelRequested, this, &MainWindow::cancelRestore);
    connect(m_restoreDialog, &RestoreDialog::openLogRequested, this, &MainWindow::openLogFile);
    m_restoreDialog->show();
}

void MainWindow::beginRestore(const RestoreRequest &request)
{
    if (m_restoreRunning) {
        return;
    }
    const DiskInfo &disk = request.disk;
    m_logger.log(
        QStringLiteral("Restore requested for %1 as %2 %3")
            .arg(describeDisk(disk), partitionStyleLabel(request.style), fileSystemTypeName(request.fileSystem)));
    m_logger.logFileOnly(QStringLiteral(
                             "Target details — device %1, bus %2, sector size %3, mounted at [%4], "
                             "labels [%5]")
                             .arg(
                                 disk.deviceId,
                                 busTypeName(disk.busType),
                                 QString::number(disk.sectorSize),
                                 disk.mountPoints.join(QStringLiteral(", ")),
                                 disk.labels.join(QStringLiteral(", "))));
    m_restoreFileSystem = request.fileSystem;

    setRunning(true);
    setStatus(StatusKind::Running, tr("Restoring %1...").arg(disk.displayId));

    auto *thread = new QThread(this);
    auto *worker = new RestoreWorker(m_service, request);
    worker->moveToThread(thread);
    m_restoreThread = thread;
    m_restoreWorker = worker;

    connect(thread, &QThread::started, worker, &RestoreWorker::run);
    connect(worker, &RestoreWorker::progress, this, &MainWindow::onRestoreProgress);
    connect(worker, &RestoreWorker::logMessage, &m_logger, &Logger::log, Qt::QueuedConnection);
    connect(worker, &RestoreWorker::logFileOnly, &m_logger, &Logger::logFileOnly, Qt::QueuedConnection);
    // Detail lines count too: a long mkfs that keeps talking is not a stall.
    connect(worker, &RestoreWorker::logMessage, this, &MainWindow::noteRestoreActivity);
    connect(worker, &RestoreWorker::logFileOnly, this, &MainWindow::noteRestoreActivity);
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
    if (!m_restoreRunning || !m_restoreWorker || m_restoreStep >= m_service.firstDestructiveStep()) {
        return;
    }

    m_restoreWorker->requestCancel();
    if (m_restoreDialog) {
        m_restoreDialog->setCancelling();
    }
}

void MainWindow::onRestoreProgress(int step, int totalSteps, const QString &message)
{
    m_restoreStep = step;
    noteRestoreActivity();
    m_stallNoticeShown = false;
    if (m_restoreDialog && !m_restoreDialog->setProgress(step, totalSteps, message)) {
        m_logger.logFileOnly(QStringLiteral(
                                 "Restore window ignored progress step %1 of %2; it does not "
                                 "follow the step it is showing.")
                                 .arg(step)
                                 .arg(totalSteps));
    }
}

void MainWindow::noteRestoreActivity()
{
    m_restoreActivity.restart();
}

// Display only: a stalled device usually recovers, and the backend handles it if not.
// Before step 1 the quiet belongs to the authentication dialog, so it is not timed.
void MainWindow::onStallTick()
{
    if (!m_restoreRunning || m_restoreStep == 0 || !m_restoreDialog) {
        return;
    }

    const qint64 seconds = m_restoreActivity.elapsed() / 1000;
    if (seconds < StallCountSeconds) {
        if (m_stallNoticeShown) {
            m_stallNoticeShown = false;
            m_restoreDialog->setNotice(
                tr("The device is responding again. Keep it connected until the restore finishes."));
        }
        return;
    }

    m_stallNoticeShown = true;
    const QString silence = QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    if (seconds < StallExplainSeconds) {
        m_restoreDialog->setNotice(
            tr("No response from the device for %1. Keep it connected while the system waits.").arg(silence));
        return;
    }

    // Before the first write the disk is unchanged, but the system may still be
    // flushing to it (a likely place to stall), so unplugging is never called "safe".
    if (m_restoreStep < m_service.firstDestructiveStep()) {
        m_restoreDialog->setNotice(
            tr("The device has not responded for %1. Some sticks stall for minutes and then recover on "
               "their own. The restore has not changed the disk, but the system may still be flushing "
               "its own writes to it — unplug only if it never recovers, and expect to run the restore "
               "again afterwards.")
                .arg(silence));
    } else {
        m_restoreDialog->setNotice(
            tr("The device has not responded for %1. Some sticks stall for minutes and then recover on "
               "their own. The disk is being rewritten — leave it plugged in and wait.")
                .arg(silence));
    }
}

// The dialog keeps the outcome; the main window reflects the freshly scanned disks.
void MainWindow::onRestoreFailed(const QString &message)
{
    if (m_restoreDialog) {
        m_restoreDialog->finishFailure(message);
    }
    setRunning(false);
    refreshDisks();
}

void MainWindow::onRestoreCancelled()
{
    if (m_restoreDialog) {
        m_restoreDialog->finishCancelled(tr("The restore stopped before erasing the disk."));
    }
    setRunning(false);
    m_logger.log(QStringLiteral(
        "Restore cancelled before erasing the disk; existing volumes may need to be mounted "
        "again or have their drive letters reassigned"));
    refreshDisks();
}

void MainWindow::onRestoreFinished(const QString &location)
{
    const QString summary = tr("The disk is now one %1 volume labelled %2 at %3.")
                                .arg(fileSystemTypeName(m_restoreFileSystem), RestoreVolumeLabel, location);
    if (m_restoreDialog) {
        m_restoreDialog->finishSuccess(summary);
    }
    setRunning(false);
    refreshDisks();
}

void MainWindow::setRunning(bool running)
{
    m_restoreRunning = running;

    if (running) {
        m_restoreStep = 0;
        m_stallNoticeShown = false;
        m_restoreActivity.start();
        m_stallTimer->start();
    } else {
        m_stallTimer->stop();
    }

    m_diskList->setEnabled(!running);
    m_refreshButton->setEnabled(!running);
    m_languageButton->setEnabled(!running);
    m_styleCombo->setEnabled(!running && selectedDisk() != nullptr);
    m_fileSystemCombo->setEnabled(!running && selectedDisk() != nullptr);

    m_restoreButton->setEnabled(!running);

    if (!running) {
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

    m_statusLabel->setStyleSheet(QStringLiteral(
                                     "QLabel#statusBadge { background: rgba(%1, %2, %3, %4); color: %5; "
                                     "border-radius: 8px; padding: 8px 12px; font-weight: 700; }")
                                     .arg(bg.red())
                                     .arg(bg.green())
                                     .arg(bg.blue())
                                     .arg(bg.alpha())
                                     .arg(fg.name(QColor::HexRgb)));
}

void MainWindow::appendLog(const QString &message)
{
    if (m_log) {
        m_log->append(message);
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    // The restore dialog stays modal for the whole attempt, so only this window is live.
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
    QMainWindow::changeEvent(event);
}

void MainWindow::retranslateUi()
{
    if (!m_subtitle || !m_restoreButton || !m_languageButton) {
        return;
    }

    applyTranslatedText();

    // Rebuilding the list passes through row 0, which is not a selection to act on.
    {
        const QSignalBlocker blocker(*m_diskList);
        const int row = m_diskList->currentRow();
        renderDisks();
        if (row >= 0 && row < m_diskList->count()) {
            m_diskList->setCurrentRow(row);
        }
    }
    onSelectionChanged();
}

void MainWindow::applyTranslatedText()
{
    m_subtitle->setText(tr("A fresh start for your USB drive."));
    // On an installed Linux build the helper, not this process, is root, so the
    // badge must not claim ROOT there.
    switch (m_service.privilegeMode()) {
    case PrivilegeMode::Elevated:
#ifdef Q_OS_WIN
        m_privilegeBadge->setText(QStringLiteral("ADMINISTRATOR"));
#else
        m_privilegeBadge->setText(QStringLiteral("ROOT"));
#endif
        m_privilegeBadge->setToolTip(tr("Raw disk access needs elevated permission, and this process runs with it."));
        break;
    case PrivilegeMode::Helper:
        m_privilegeBadge->setText(tr("USER"));
        m_privilegeBadge->setToolTip(
            tr("The app runs as an ordinary user. When a restore starts, the system asks for permission "
               "and a separate root helper does the writing."));
        break;
    case PrivilegeMode::None:
        // Unreachable: main() refuses to open this window without a route to privilege.
        m_privilegeBadge->setText(tr("NO ACCESS"));
        m_privilegeBadge->setToolTip(m_service.privilegeHint());
        break;
    }
    m_activityButton->setText(tr("Activity"));
    m_activityButton->setToolTip(tr("Every step of this session, and a link to the log file."));
    m_languageButton->setText(languageButtonText(m_settings.language));
    m_themeButton->setText(themeButtonText(m_theme));
    m_aboutButton->setText(tr("About"));
    m_diskCard->setTitle(tr("01 / Select a drive"));
    m_diskList->setAccessibleName(tr("Detected USB disks"));
    m_refreshButton->setText(tr("&Refresh"));
    m_detailCard->setTitle(tr("Drive details"));
    m_restoreCard->setTitle(tr("02 / Set up your restore"));
    m_partitionLabel->setText(tr("Partition layout"));
    m_filesystemLabel->setText(tr("Filesystem"));
    {
        const QSignalBlocker styleBlocker(m_styleCombo);
        const int gpt = m_styleCombo->findData(static_cast<int>(PartitionStyle::Gpt));
        const int mbr = m_styleCombo->findData(static_cast<int>(PartitionStyle::Mbr));
        if (gpt >= 0) {
            m_styleCombo->setItemText(gpt, tr("GPT (recommended)"));
        }
        if (mbr >= 0) {
            m_styleCombo->setItemText(mbr, tr("MBR (older devices)"));
        }
    }
    m_styleCombo->setAccessibleName(tr("Partition style"));
    m_styleCombo->setToolTip(
        tr("GPT is what current systems expect and what Windows itself writes.\n"
           "MBR is for devices that predate it and refuse to read a GPT stick:\n"
           "BIOS-era PCs, car stereos, older TVs and set-top boxes."));
    {
        const QSignalBlocker filesystemBlocker(m_fileSystemCombo);
        for (int index = 0; index < m_fileSystemCombo->count(); ++index) {
            m_fileSystemCombo->setItemText(
                index, fileSystemTypeLabel(static_cast<FileSystemType>(m_fileSystemCombo->itemData(index).toInt())));
        }
    }
    m_fileSystemCombo->setAccessibleName(tr("Filesystem"));
    m_fileSystemCombo->setToolTip(
        tr("exFAT (recommended) works on Windows, Linux and macOS.\n"
           "FAT32 is for older devices; files cannot be larger than 4 GB.\n"
           "NTFS is Windows' own filesystem; some cameras and consoles will not read it.\n"
           "ext4 is Linux-only; Windows will not mount it without extra software."));
    m_statusLabel->setAccessibleName(tr("Restore status"));
    m_restoreButton->setText(tr("Restore USB"));
    m_restoreButton->setAccessibleName(tr("Restore the selected USB disk"));
    m_warningTitle->setText(tr("Restoring erases every partition and file on the selected drive."));
    m_warningDetail->setText(
        tr("Check the model and capacity before confirming. Boot, system, offline, read-only and non-USB disks "
           "are refused."));
    m_log->setAccessibleName(tr("Activity log"));
    if (m_activityDialog) {
        applyActivityDialogText();
    }

    keepButtonTextVisible(m_activityButton);
    keepButtonTextVisible(m_languageButton);
    keepButtonTextVisible(m_themeButton);
    keepButtonTextVisible(m_aboutButton);
    keepButtonTextVisible(m_restoreButton);
    fitMinimumWidthToContent();
}

void MainWindow::applyActivityDialogText()
{
    m_activityDialog->setWindowTitle(tr("Activity"));
    m_activityCaption->setText(
        tr("Every step of this session. The same lines are written to the log "
           "file, which survives a restart."));
    m_openLogButton->setText(tr("Open log file"));
    m_activityCloseButton->setText(tr("Close"));
}

// Horizontal scrolling is off, so a page wider than the viewport would be cut off with no way to reach it.
void MainWindow::fitMinimumWidthToContent()
{
    auto *scroll = qobject_cast<QScrollArea *>(centralWidget());
    QWidget *page = scroll ? scroll->widget() : nullptr;
    if (!page) {
        return;
    }
    // Nested layouts keep their cached sizes until activated.
    page->layout()->activate();
    setMinimumWidth(qMax(MinimumWindowWidth, page->minimumSizeHint().width() + 36));
    if (width() < minimumWidth()) {
        resize(minimumWidth(), height());
    }
}

void MainWindow::cycleLanguage()
{
    if (m_restoreRunning) {
        return;
    }
    switch (m_settings.language) {
    case Language::System:
        m_settings.language = Language::English;
        break;
    case Language::English:
        m_settings.language = Language::German;
        break;
    case Language::German:
        m_settings.language = Language::System;
        break;
    }
    saveSettings();
    if (m_translations) {
        m_translations->load(m_settings.language);
    }
    m_languageButton->setText(languageButtonText(m_settings.language));
    // Auto and English can resolve to the same catalog, and then no LanguageChange arrives to refit the header.
    keepButtonTextVisible(m_languageButton);
    fitMinimumWidthToContent();
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
    about.setObjectName(QStringLiteral("aboutDialog"));
    about.setWindowTitle(tr("About USB Restoration Tool"));
    about.setIconPixmap(QIcon(QStringLiteral(":/icons/app.svg")).pixmap(QSize(64, 64), devicePixelRatioF()));
    about.setText(tr("USB Restoration Tool %1").arg(QString::fromLatin1(USBRESTORE_APP_VERSION)));
    about.setInformativeText(tr("A fresh start for your USB drive.\n\n"
                                "Created by %1\n\n"
                                "Restores a USB drive written by an ISO writer, Linux installer, or boot "
                                "media tool to one GPT or MBR partition labelled USB. Choose exFAT, "
                                "FAT32, or NTFS; Linux can also format ext4.\n\n"
                                "Confirmation, progress and the result stay in one window. Nothing is "
                                "erased until you tick the acknowledgement and confirm.\n\n"
                                "Only USB disks are listed, and boot, system, offline, and read-only "
                                "disks are refused.\n\n"
                                "Published builds are a Windows portable package, a Debian package, a "
                                "Fedora RPM, and a Linux AppImage.")
                                 .arg(QString::fromLatin1(AppAuthor)));
    about.setStandardButtons(QMessageBox::Ok);
    QPushButton *githubButton = about.addButton(tr("Open GitHub"), QMessageBox::ActionRole);
    about.exec();
    if (about.clickedButton() == githubButton) {
        openExternally(QString::fromLatin1(AppProfileUrl), false);
    }
}

void MainWindow::openLogFile()
{
    const QString path = m_logger.path();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(this, tr("No log file"), tr("No log file has been written yet."));
        return;
    }
    openExternally(path, true);
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
        // The restore dialog is window-modal, so the explanation has to be its child to be seen.
        QWidget *parent = m_restoreDialog && m_restoreDialog->isVisible()
                              ? static_cast<QWidget *>(m_restoreDialog.data())
                              : static_cast<QWidget *>(this);
        // Closing is refused either way. Only a rewrite already in progress would leave the disk unreadable.
        const bool writing = m_restoreStep >= m_service.firstDestructiveStep();
        QMessageBox::information(
            parent,
            tr("Restore in progress"),
            writing ? tr("A restore is writing to the disk. Wait for it to finish before closing; "
                         "stopping now would leave the disk unusable.")
                    : tr("A restore is still running. Wait for it to finish before closing. "
                         "The disk has not been changed yet, but closing now would stop the restore."));
        event->ignore();
        return;
    }

    waitForRestoreThread(5000);
    saveSettings();
    event->accept();
}

} // namespace usbrestore

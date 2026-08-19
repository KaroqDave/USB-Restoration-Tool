#include "gui/main_window.h"

#include "core/safety.h"
#include "platform/restore_worker.h"

#ifdef Q_OS_WIN
#include "win/windows_util.h"
#endif

#include <QApplication>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
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
#include <QListView>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSize>
#include <QStyle>
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

// What every restore produces. Named once so the plan shown to the user and
// the label actually written cannot drift apart.
const QString RestoreVolumeLabel = QStringLiteral("USB");

// How long a restore step may stay silent before the progress bar starts
// counting the silence, and before the status spells out that the device may
// have stalled. A USB stick can sit in uninterruptible sleep for minutes and
// recover — seen on hardware, 2026-08-19: four and a half minutes in D state
// while the GUI gave no hint which side was stuck.
constexpr qint64 StallCountSeconds = 15;
constexpr qint64 StallExplainSeconds = 60;

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

void styleComboPopup(QComboBox *combo)
{
    // A QListView popup so the item styling in the stylesheet applies; the
    // native popup ignores it.
    auto *popupView = new QListView(combo);
    popupView->setFrameShape(QFrame::NoFrame);
    combo->setView(popupView);
    for (int i = 0; i < combo->count(); ++i) {
        combo->setItemData(i, QSize(0, 32), Qt::SizeHintRole);
    }
}

QString diskListEntry(const DiskInfo &disk)
{
    const QString name = disk.name.isEmpty() ? QStringLiteral("USB disk") : disk.name;
    const QString mounts =
        disk.mountPoints.isEmpty() ? QStringLiteral("not mounted") : disk.mountPoints.join(QStringLiteral(", "));
    return QStringLiteral("%1  ·  %2\n%3  ·  %4  ·  %5")
        .arg(
            disk.displayId.isEmpty() ? disk.deviceId : disk.displayId,
            name,
            formatByteSize(disk.size),
            partitionStyleName(disk.partitionStyle),
            mounts);
}

// Opens a file or link without dragging the elevated process's privileges into
// whatever handles it. On Windows that means routing through explorer, which
// runs as the logged-in user; elsewhere QDesktopServices is already right.
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

MainWindow::MainWindow(DiskService &service, Theme initialTheme, QWidget *parent)
    : QMainWindow(parent), m_service(service), m_theme(initialTheme)
{
    setWindowTitle(QStringLiteral("USB Restoration Tool"));
    // Small enough to sit comfortably on a laptop screen. Moving the activity
    // log into its own window is what bought the room.
    setMinimumSize(760, 560);
    resize(880, 640);

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
    mainLayout->setContentsMargins(24, 20, 24, 18);
    mainLayout->setSpacing(14);

    mainLayout->addLayout(buildHeaderLayout());
    mainLayout->addWidget(buildDiskSection());
    mainLayout->addWidget(buildDetailSection());
    mainLayout->addWidget(buildRestoreSection());
    mainLayout->addLayout(buildActionLayout());
    mainLayout->addWidget(buildFooterWarning());
    mainLayout->addStretch();

    scroll->setWidget(central);
    setCentralWidget(scroll);

    m_stallTimer = new QTimer(this);
    m_stallTimer->setInterval(1000);
    connect(m_stallTimer, &QTimer::timeout, this, &MainWindow::onStallTick);

    // Lives outside the layout: it is the activity dialog's contents, and it
    // accumulates lines whether or not that dialog is open.
    m_log = new QTextEdit;
    m_log->setObjectName(QStringLiteral("activityLog"));
    m_log->setReadOnly(true);
    m_log->setMinimumSize(560, 320);
    m_log->setAccessibleName(QStringLiteral("Activity log"));

    refreshStatusBadge();
}

QLayout *MainWindow::buildHeaderLayout()
{
    auto *header = new QGridLayout();
    header->setHorizontalSpacing(12);

    auto *title = new QLabel(QStringLiteral("USB Restoration Tool"));
    title->setObjectName(QStringLiteral("title"));
    auto *subtitle =
        new QLabel(QStringLiteral("Restore a USB drive written by an ISO writer back to one clean volume."));
    subtitle->setObjectName(QStringLiteral("subtitle"));

    // The badge states how this run actually gets its privilege, which on an
    // installed Linux build is precisely *not* running as root — the helper
    // is. Claiming ROOT there would misreport the privilege split it exists
    // to advertise.
    auto *badge = new QLabel;
    badge->setObjectName(QStringLiteral("adminBadge"));
    switch (m_service.privilegeMode()) {
    case PrivilegeMode::Elevated:
#ifdef Q_OS_WIN
        badge->setText(QStringLiteral("ADMINISTRATOR"));
#else
        badge->setText(QStringLiteral("ROOT"));
#endif
        badge->setToolTip(
            QStringLiteral("Raw disk access needs elevated permission, and this process runs with it."));
        break;
    case PrivilegeMode::Helper:
        badge->setText(QStringLiteral("USER"));
        badge->setToolTip(QStringLiteral(
            "The app runs as an ordinary user. When a restore starts, the system asks for permission "
            "and a separate root helper does the writing."));
        break;
    case PrivilegeMode::None:
        // Unreachable in practice: main() refuses to open this window without
        // a route to privilege. Stated anyway rather than left blank.
        badge->setText(QStringLiteral("NO ACCESS"));
        badge->setToolTip(m_service.privilegeHint());
        break;
    }

    auto *headerButtons = new QHBoxLayout();
    headerButtons->setSpacing(6);
    m_activityButton = styledButton(QStringLiteral("Activity"), "text");
    m_activityButton->setToolTip(QStringLiteral("Every step of this session, and a link to the log file."));
    connect(m_activityButton, &QPushButton::clicked, this, &MainWindow::showActivity);
    m_themeButton = styledButton(themeButtonText(m_theme), "text");
    connect(m_themeButton, &QPushButton::clicked, this, &MainWindow::toggleTheme);
    auto *aboutButton = styledButton(QStringLiteral("About"), "text");
    connect(aboutButton, &QPushButton::clicked, this, &MainWindow::showAbout);
    headerButtons->addWidget(badge);
    headerButtons->addWidget(m_activityButton);
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
    m_diskList->setMinimumHeight(120);
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
    layout->setSpacing(6);

    m_detailTitle = new QLabel(QStringLiteral("No disk selected"));
    m_detailTitle->setObjectName(QStringLiteral("fieldLabel"));
    m_detailTitle->setWordWrap(true);

    m_detailValues = new QLabel(QStringLiteral("Only disks reported on the USB bus are listed."));
    m_detailValues->setObjectName(QStringLiteral("fieldValue"));
    m_detailValues->setWordWrap(true);
    m_detailValues->setTextInteractionFlags(Qt::TextSelectableByMouse);

    layout->addWidget(m_detailTitle);
    layout->addWidget(m_detailValues);
    return box;
}

QWidget *MainWindow::buildRestoreSection()
{
    auto *box = card(QStringLiteral("Restore"));
    auto *layout = new QGridLayout(box);
    layout->setHorizontalSpacing(12);
    layout->setVerticalSpacing(10);

    m_styleCombo = new QComboBox;
    m_styleCombo->addItem(QStringLiteral("GPT (recommended)"), static_cast<int>(PartitionStyle::Gpt));
    m_styleCombo->addItem(QStringLiteral("MBR (older devices)"), static_cast<int>(PartitionStyle::Mbr));
    m_styleCombo->setCursor(Qt::PointingHandCursor);
    m_styleCombo->setAccessibleName(QStringLiteral("Partition style"));
    m_styleCombo->setToolTip(QStringLiteral(
        "GPT is what current systems expect and what Windows itself writes.\n"
        "MBR is for devices that predate it and refuse to read a GPT stick:\n"
        "BIOS-era PCs, car stereos, older TVs and set-top boxes."));
    styleComboPopup(m_styleCombo);
    connect(m_styleCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateRestoreState);

    m_fileSystemCombo = new QComboBox;
    m_fileSystemCombo->setCursor(Qt::PointingHandCursor);
    m_fileSystemCombo->setAccessibleName(QStringLiteral("Filesystem"));
    m_fileSystemCombo->setToolTip(QStringLiteral(
        "exFAT (recommended) works on Windows, Linux and macOS.\n"
        "FAT32 is for older devices; files cannot be larger than 4 GB.\n"
        "NTFS is Windows' own filesystem; some cameras and consoles will not read it.\n"
        "ext4 is Linux-only; Windows will not mount it without extra software."));
    for (const FileSystemType type : m_service.supportedFileSystems()) {
        m_fileSystemCombo->addItem(fileSystemTypeLabel(type), static_cast<int>(type));
    }
    styleComboPopup(m_fileSystemCombo);
    connect(m_fileSystemCombo, &QComboBox::currentIndexChanged, this, &MainWindow::updateRestoreState);

    m_planLabel = new QLabel;
    m_planLabel->setObjectName(QStringLiteral("footnote"));
    m_planLabel->setWordWrap(true);

    m_statusLabel = new QLabel(QStringLiteral("Select a USB disk to restore."));
    m_statusLabel->setObjectName(QStringLiteral("statusBadge"));
    m_statusLabel->setWordWrap(true);
    m_statusLabel->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Minimum);
    m_statusLabel->setAccessibleName(QStringLiteral("Restore status"));

    m_detailLabel = new QLabel;
    m_detailLabel->setObjectName(QStringLiteral("footnote"));
    m_detailLabel->setWordWrap(true);

    layout->addWidget(fieldLabel(QStringLiteral("Layout")), 0, 0);
    layout->addWidget(m_styleCombo, 0, 1);
    layout->addWidget(fieldLabel(QStringLiteral("Filesystem")), 1, 0);
    layout->addWidget(m_fileSystemCombo, 1, 1);
    layout->addWidget(m_planLabel, 2, 0, 1, 2);
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
    m_progress->setRange(0, m_service.totalRestoreSteps());
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

QWidget *MainWindow::buildFooterWarning()
{
    auto *warning = new QLabel(QStringLiteral(
        "Restoring erases every partition and file on the selected disk. Disks reported as boot, system, offline, "
        "read-only, or on any bus other than USB are never offered."));
    warning->setObjectName(QStringLiteral("footnote"));
    warning->setWordWrap(true);
    return warning;
}

QDialog *MainWindow::activityDialog()
{
    if (m_activityDialog) {
        return m_activityDialog;
    }

    auto *dialog = new QDialog(this);
    dialog->setWindowTitle(QStringLiteral("Activity"));
    dialog->setModal(false);
    dialog->resize(680, 420);

    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(16, 16, 16, 16);
    layout->setSpacing(12);

    auto *caption = new QLabel(QStringLiteral(
        "Every step of this session. The same lines are written to the log "
        "file, which survives a restart."));
    caption->setObjectName(QStringLiteral("footnote"));
    caption->setWordWrap(true);
    layout->addWidget(caption);
    layout->addWidget(m_log, 1);

    auto *buttons = new QHBoxLayout();
    auto *openLogButton = styledButton(QStringLiteral("Open log file"), "secondary");
    connect(openLogButton, &QPushButton::clicked, this, &MainWindow::openLogFile);
    auto *closeButton = styledButton(QStringLiteral("Close"), "secondary");
    connect(closeButton, &QPushButton::clicked, dialog, &QDialog::hide);
    buttons->addWidget(openLogButton);
    buttons->addStretch();
    buttons->addWidget(closeButton);
    layout->addLayout(buttons);

    m_activityDialog = dialog;
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
    m_settings.theme = m_theme;
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
    switch (type) {
    case FileSystemType::ExFat:
    case FileSystemType::Fat32:
    case FileSystemType::Ntfs:
    case FileSystemType::Ext4:
        return type;
    }
    return FileSystemType::ExFat;
}

void MainWindow::refreshDisks()
{
    if (m_restoreRunning) {
        return;
    }

    const QSignalBlocker blocker(*m_diskList);
    QString error;
    m_guard = m_service.restoreGuard();
    m_disks = m_service.listUsbDisks(&error);
    renderDisks();
    onSelectionChanged();

    // Reported after the selection has been rebuilt, so the failure is the last
    // word on screen rather than being overwritten by the empty-list status.
    if (!error.isEmpty()) {
        m_logger.log(QStringLiteral("Scan failed: %1").arg(error));
        setStatus(StatusKind::Error, QStringLiteral("Could not scan for USB disks."), error);
    }
}

void MainWindow::renderDisks()
{
    m_diskList->clear();
    for (const DiskInfo &disk : m_disks) {
        auto *item = new QListWidgetItem(diskListEntry(disk), m_diskList);
        QString reason;
        if (!isSafeRestoreTarget(disk, m_guard, &reason)) {
            item->setToolTip(reason);
        }
    }

    if (m_disks.isEmpty()) {
        m_diskCount->setText(QStringLiteral("No USB disks detected. Plug the drive in and select Refresh."));
    } else {
        m_diskCount->setText(
            m_disks.size() == 1 ? QStringLiteral("1 USB disk detected.")
                                : QStringLiteral("%1 USB disks detected.").arg(m_disks.size()));
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
        m_detailTitle->setText(QStringLiteral("No disk selected"));
        m_detailValues->setText(QStringLiteral("Only disks reported on the USB bus are listed."));
        return;
    }

    m_detailTitle->setText(describeDisk(*disk));
    m_detailValues->setText(QStringLiteral(
                                "Bus: %1    Current layout: %2    Sector size: %3 bytes\n"
                                "Filesystems: %4\n"
                                "Mounted at: %5\n"
                                "Volume labels: %6")
                                .arg(busTypeName(disk->busType), partitionStyleName(disk->partitionStyle))
                                .arg(disk->sectorSize)
                                .arg(joinOrDash(disk->fileSystems),
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
    m_planLabel->setText(QStringLiteral(
                             "Every partition and file on the selected disk is erased and replaced with "
                             "one %1 partition formatted %2 and labelled %3.")
                             .arg(style, fileSystem, RestoreVolumeLabel));

    const DiskInfo *disk = selectedDisk();
    if (!disk) {
        m_styleCombo->setEnabled(false);
        m_fileSystemCombo->setEnabled(false);
        m_restoreButton->setEnabled(false);
        setStatus(
            StatusKind::Info,
            QStringLiteral("Select a USB disk to restore."),
            m_disks.isEmpty() ? QStringLiteral("Nothing to restore until a USB disk is plugged in.") : QString());
        return;
    }

    m_styleCombo->setEnabled(true);
    m_fileSystemCombo->setEnabled(true);

    QString reason;
    if (!isSafeRestoreTarget(*disk, m_guard, &reason)) {
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Blocked, QStringLiteral("This disk will not be restored."), reason);
        return;
    }

    if (!m_service.canFormatFileSystem(selectedFileSystem(), *disk, &reason)) {
        m_restoreButton->setEnabled(false);
        setStatus(StatusKind::Blocked, QStringLiteral("This filesystem cannot be used on this disk."), reason);
        return;
    }

    m_restoreButton->setEnabled(true);

    const QString warning = largeRestoreTargetWarning(*disk);
    if (!warning.isEmpty()) {
        setStatus(StatusKind::Warning, QStringLiteral("Check this is the right disk."), warning);
        return;
    }
    setStatus(
        StatusKind::Ready,
        QStringLiteral("Ready to restore %1 as %2 %3.").arg(describeDisk(*disk), style, fileSystem),
        QStringLiteral("Contents: %1").arg(joinOrDash(disk->labels)));
}

bool MainWindow::confirmErase(const DiskInfo &disk, PartitionStyle style, FileSystemType fileSystem)
{
    QString summary = QStringLiteral(
                          "%1\n\nMounted at: %2\nVolume labels: %3\n\n"
                          "Every partition and file on this disk will be erased and replaced with one %4 "
                          "partition formatted %5 and labelled %6.")
                          .arg(
                              describeDisk(disk),
                              joinOrDash(disk.mountPoints),
                              joinOrDash(disk.labels),
                              partitionStyleLabel(style),
                              fileSystemTypeName(fileSystem),
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

    // The gate is here rather than on the main window: it is attached to the
    // summary naming this specific disk, so what is being agreed to is on
    // screen at the moment it is agreed to.
    auto *understood = new QCheckBox(
        QStringLiteral("I understand that everything on %1 will be permanently erased.").arg(disk.displayId));
    confirm.setCheckBox(understood);

    QPushButton *eraseButton = confirm.addButton(QStringLiteral("Erase and restore"), QMessageBox::DestructiveRole);
    QPushButton *cancelButton = confirm.addButton(QMessageBox::Cancel);
    confirm.setDefaultButton(cancelButton);
    eraseButton->setEnabled(false);
    connect(understood, &QCheckBox::toggled, eraseButton, &QPushButton::setEnabled);

    confirm.exec();
    return confirm.clickedButton() == eraseButton && understood->isChecked();
}

void MainWindow::startRestore()
{
    const DiskInfo *selected = selectedDisk();
    if (!selected) {
        return;
    }

    QString reason;
    if (!isSafeRestoreTarget(*selected, m_guard, &reason)) {
        QMessageBox::critical(this, QStringLiteral("Restore refused"), reason);
        updateRestoreState();
        return;
    }

    const DiskInfo disk = *selected;
    const PartitionStyle style = selectedPartitionStyle();
    const FileSystemType fileSystem = selectedFileSystem();
    if (!m_service.canFormatFileSystem(fileSystem, disk, &reason)) {
        QMessageBox::critical(this, QStringLiteral("Restore refused"), reason);
        updateRestoreState();
        return;
    }
    if (!confirmErase(disk, style, fileSystem)) {
        return;
    }

    m_logger.log(QStringLiteral("Restore requested for %1 as %2 %3")
                     .arg(describeDisk(disk), partitionStyleLabel(style), fileSystemTypeName(fileSystem)));
    m_logger.logFileOnly(QStringLiteral(
                             "Target details — device %1, bus %2, sector size %3, mounted at [%4], "
                             "labels [%5]")
                             .arg(disk.deviceId, busTypeName(disk.busType))
                             .arg(disk.sectorSize)
                             .arg(disk.mountPoints.join(QStringLiteral(", ")), disk.labels.join(QStringLiteral(", "))));

    RestoreRequest request;
    request.disk = disk;
    request.style = style;
    request.fileSystem = fileSystem;
    request.volumeLabel = RestoreVolumeLabel;
    request.guard = m_guard;

    setRunning(true);
    setStatus(StatusKind::Running, QStringLiteral("Restoring %1...").arg(disk.displayId));

    auto *thread = new QThread(this);
    auto *worker = new RestoreWorker(m_service, request);
    worker->moveToThread(thread);
    m_restoreThread = thread;
    m_restoreWorker = worker;

    connect(thread, &QThread::started, worker, &RestoreWorker::run);
    connect(worker, &RestoreWorker::progress, this, &MainWindow::onRestoreProgress);
    connect(worker, &RestoreWorker::logMessage, &m_logger, &Logger::log, Qt::QueuedConnection);
    connect(worker, &RestoreWorker::logFileOnly, &m_logger, &Logger::logFileOnly, Qt::QueuedConnection);
    // Any line from the backend is proof the device is still answering, detail
    // lines included — a long mkfs that keeps talking is not a stall.
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
    if (!m_restoreRunning || !m_restoreWorker) {
        return;
    }

    m_restoreWorker->requestCancel();
    m_restoreButton->setEnabled(false);
    setStatus(
        StatusKind::Running,
        QStringLiteral("Stopping..."),
        QStringLiteral(
            "The restore stops at the next checkpoint. Once the disk has been written to there is "
            "nothing left to stop, and it runs to completion."));
}

void MainWindow::onRestoreProgress(int step, int totalSteps, const QString &message)
{
    m_restoreStep = step;
    m_restoreStepText = message;
    noteRestoreActivity();
    m_stallNoticeShown = false;

    m_progress->setRange(0, totalSteps);
    m_progress->setValue(step);
    m_progress->setFormat(QStringLiteral("%1 / %2 — %3").arg(step).arg(totalSteps).arg(message));
    m_progress->setAccessibleDescription(message);
    setStatus(StatusKind::Running, message, QStringLiteral("Step %1 of %2.").arg(step).arg(totalSteps));
}

void MainWindow::noteRestoreActivity()
{
    m_restoreActivity.restart();
}

// Once a second while a restore runs: how long has the backend been silent?
// Before the first step there is nothing to time — that quiet belongs to the
// authentication dialog, where the user is allowed to think. The elapsed count
// and the explanation are display only; nothing here stops or decides
// anything, because a stalled device usually recovers and the backend already
// handles the failure if it does not.
void MainWindow::onStallTick()
{
    if (!m_restoreRunning || m_restoreStep == 0) {
        return;
    }

    const qint64 seconds = m_restoreActivity.elapsed() / 1000;
    if (seconds < StallCountSeconds) {
        if (m_stallNoticeShown) {
            // The device came back: put the plain step display back.
            m_stallNoticeShown = false;
            m_progress->setFormat(QStringLiteral("%1 / %2 — %3")
                                      .arg(m_restoreStep)
                                      .arg(m_progress->maximum())
                                      .arg(m_restoreStepText));
            m_detailLabel->setText(
                QStringLiteral("Step %1 of %2.").arg(m_restoreStep).arg(m_progress->maximum()));
        }
        return;
    }

    m_stallNoticeShown = true;
    const QString silence =
        QStringLiteral("%1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0'));
    m_progress->setFormat(QStringLiteral("%1 / %2 — %3 — no response for %4")
                              .arg(m_restoreStep)
                              .arg(m_progress->maximum())
                              .arg(m_restoreStepText, silence));

    if (seconds < StallExplainSeconds) {
        return;
    }

    // The one sentence that must not be wrong: unplugging is called safe only
    // strictly before the backend's first write.
    if (m_restoreStep < m_service.firstDestructiveStep()) {
        m_detailLabel->setText(QStringLiteral(
            "The device has not responded for %1. Some sticks stall for minutes and then recover on "
            "their own. Nothing has been written to the disk yet, so if it never recovers, unplugging "
            "the stick is safe.")
                                   .arg(silence));
    } else {
        m_detailLabel->setText(QStringLiteral(
            "The device has not responded for %1. Some sticks stall for minutes and then recover on "
            "their own. The disk is being rewritten — leave it plugged in and wait.")
                                   .arg(silence));
    }
}

// The three endings share a shape: stop, rescan — because the disk on screen
// has just changed underneath the list — and only then set the status, so the
// rescan's own "ready to restore" verdict does not overwrite the outcome.
void MainWindow::onRestoreFailed(const QString &message)
{
    setRunning(false);
    refreshDisks();
    setStatus(StatusKind::Error, QStringLiteral("The restore failed."), message);
    QMessageBox::critical(
        this, QStringLiteral("Restore failed"), QStringLiteral("%1\n\nLog file:\n%2").arg(message, m_logger.path()));
}

void MainWindow::onRestoreCancelled()
{
    setRunning(false);
    m_logger.log(QStringLiteral("Restore cancelled before any change was made"));
    refreshDisks();
    setStatus(StatusKind::Info, QStringLiteral("Restore cancelled."), QStringLiteral("The disk was not changed."));
}

void MainWindow::onRestoreFinished(const QString &location)
{
    setRunning(false);
    refreshDisks();
    m_progress->setValue(m_progress->maximum());
    m_progress->setFormat(QStringLiteral("Restore complete"));
    setStatus(
        StatusKind::Success,
        QStringLiteral("Restore complete."),
        QStringLiteral("The disk is now one %1 volume labelled %2 at %3.")
            .arg(fileSystemTypeName(selectedFileSystem()), RestoreVolumeLabel, location));
    QMessageBox::information(
        this,
        QStringLiteral("Restore complete"),
        QStringLiteral("The USB disk was restored. New %1: %2").arg(m_service.restoredLocationNoun(), location));
}

void MainWindow::setRunning(bool running)
{
    m_restoreRunning = running;

    if (running) {
        m_restoreStep = 0;
        m_restoreStepText.clear();
        m_stallNoticeShown = false;
        m_restoreActivity.start();
        m_stallTimer->start();
    } else {
        m_stallTimer->stop();
    }

    m_diskList->setEnabled(!running);
    m_refreshButton->setEnabled(!running);
    m_styleCombo->setEnabled(!running && selectedDisk() != nullptr);
    m_fileSystemCombo->setEnabled(!running && selectedDisk() != nullptr);

    m_restoreButton->setEnabled(true);
    m_restoreButton->setText(running ? QStringLiteral("Cancel") : QStringLiteral("Restore USB"));
    restyle(m_restoreButton, running ? "secondary" : "danger");

    if (!running) {
        m_progress->setFormat(QString());
        m_progress->setValue(0);
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
    about.setInformativeText(QStringLiteral(
                                 "Created by %1\n\n"
                                 "Restores a USB drive written by an ISO writer, Linux installer, or boot "
                                 "media tool back to one GPT or MBR partition formatted as you choose.\n\n"
                                 "Only USB disks are listed, and boot, system, offline, and read-only "
                                 "disks are refused.")
                                 .arg(QString::fromLatin1(AppAuthor)));
    about.setStandardButtons(QMessageBox::Ok);
    QPushButton *githubButton = about.addButton(QStringLiteral("Open GitHub"), QMessageBox::ActionRole);
    about.exec();
    if (about.clickedButton() == githubButton) {
        openExternally(QString::fromLatin1(AppProfileUrl), false);
    }
}

void MainWindow::openLogFile()
{
    const QString path = m_logger.path();
    if (path.isEmpty() || !QFileInfo::exists(path)) {
        QMessageBox::information(
            this, QStringLiteral("No log file"), QStringLiteral("No log file has been written yet."));
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
        // Killing the process partway through a partition rewrite leaves a disk
        // that neither the system nor the previous ISO can read, so closing
        // waits rather than offering to abandon the work.
        QMessageBox::information(
            this,
            QStringLiteral("Restore in progress"),
            QStringLiteral(
                "A restore is still running. Wait for it to finish before closing; "
                "stopping now would leave the disk unusable."));
        event->ignore();
        return;
    }

    waitForRestoreThread(5000);
    saveSettings();
    event->accept();
}

} // namespace usbrestore

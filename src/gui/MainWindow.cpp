#include "gui/MainWindow.h"

#include "win/RestoreWorker.h"

#include <QApplication>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QProgressBar>
#include <QPushButton>
#include <QTextEdit>
#include <QThread>
#include <QVBoxLayout>

namespace usbrestore {

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    buildUi();
    applyStyle();
    connect(&m_logger, &Logger::lineWritten, this, &MainWindow::appendLog);
    refreshDisks();
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_restoreThread && m_restoreThread->isRunning()) {
        event->ignore();
        QMessageBox::information(this,
                                 QStringLiteral("Restore in progress"),
                                 QStringLiteral("Wait for the current restore to finish before closing the app."));
        return;
    }

    QMainWindow::closeEvent(event);
}

void MainWindow::buildUi()
{
    setWindowTitle(QStringLiteral("USB Restoration Tool"));
    resize(1040, 680);

    auto *root = new QWidget(this);
    auto *mainLayout = new QVBoxLayout(root);
    mainLayout->setContentsMargins(20, 20, 20, 20);
    mainLayout->setSpacing(14);

    auto *header = new QWidget(root);
    auto *headerLayout = new QHBoxLayout(header);
    headerLayout->setContentsMargins(18, 14, 18, 14);

    auto *headingBox = new QVBoxLayout();
    auto *heading = new QLabel(QStringLiteral("USB Restoration Tool"), header);
    heading->setObjectName(QStringLiteral("Heading"));
    auto *subtitle = new QLabel(QStringLiteral("Restore ISO-written USB drives to one clean exFAT volume"), header);
    subtitle->setObjectName(QStringLiteral("Muted"));
    headingBox->addWidget(heading);
    headingBox->addWidget(subtitle);

    auto *adminBadge = new QLabel(QStringLiteral("ADMIN MODE"), header);
    adminBadge->setObjectName(QStringLiteral("Badge"));
    adminBadge->setAlignment(Qt::AlignCenter);
    m_refreshButton = new QPushButton(QStringLiteral("Refresh"), header);

    headerLayout->addLayout(headingBox, 1);
    headerLayout->addWidget(adminBadge);
    headerLayout->addWidget(m_refreshButton);

    mainLayout->addWidget(header);

    auto *content = new QHBoxLayout();
    content->setSpacing(14);

    auto *leftPanel = new QWidget(root);
    leftPanel->setObjectName(QStringLiteral("Panel"));
    auto *leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(16, 16, 16, 16);
    auto *diskHeading = new QLabel(QStringLiteral("USB disks"), leftPanel);
    diskHeading->setObjectName(QStringLiteral("PanelTitle"));
    m_diskList = new QListWidget(leftPanel);
    leftLayout->addWidget(diskHeading);
    leftLayout->addWidget(m_diskList, 1);

    auto *rightPanel = new QWidget(root);
    rightPanel->setObjectName(QStringLiteral("Panel"));
    auto *rightLayout = new QVBoxLayout(rightPanel);
    rightLayout->setContentsMargins(16, 16, 16, 16);
    rightLayout->setSpacing(10);

    m_title = new QLabel(QStringLiteral("Select a USB disk"), rightPanel);
    m_title->setObjectName(QStringLiteral("PanelTitle"));
    m_details = new QLabel(QStringLiteral("Only USB disks reported by Windows are shown."), rightPanel);
    m_details->setObjectName(QStringLiteral("Muted"));
    m_details->setWordWrap(true);
    m_preview = new QLabel(QStringLiteral("Restore target: GPT + exFAT, label USB"), rightPanel);
    m_preview->setObjectName(QStringLiteral("Preview"));
    m_confirmation = new QLineEdit(rightPanel);
    m_confirmation->setPlaceholderText(QStringLiteral("Type RESTORE DISK <number>"));
    m_validation = new QLabel(rightPanel);
    m_validation->setObjectName(QStringLiteral("ErrorText"));
    m_validation->setWordWrap(true);
    m_restoreButton = new QPushButton(QStringLiteral("Restore USB"), rightPanel);
    m_restoreButton->setEnabled(false);
    m_progress = new QProgressBar(rightPanel);
    m_progress->setRange(0, 0);
    m_progress->hide();
    m_log = new QTextEdit(rightPanel);
    m_log->setReadOnly(true);

    rightLayout->addWidget(m_title);
    rightLayout->addWidget(m_details);
    rightLayout->addWidget(m_preview);
    rightLayout->addWidget(m_confirmation);
    rightLayout->addWidget(m_validation);
    rightLayout->addWidget(m_restoreButton);
    rightLayout->addWidget(m_progress);
    rightLayout->addWidget(m_log, 1);

    content->addWidget(leftPanel, 1);
    content->addWidget(rightPanel, 1);
    mainLayout->addLayout(content, 1);

    setCentralWidget(root);

    connect(m_refreshButton, &QPushButton::clicked, this, &MainWindow::refreshDisks);
    connect(m_diskList, &QListWidget::currentRowChanged, this, &MainWindow::selectDisk);
    connect(m_confirmation, &QLineEdit::textChanged, this, &MainWindow::updateRestoreState);
    connect(m_restoreButton, &QPushButton::clicked, this, &MainWindow::startRestore);
}

void MainWindow::applyStyle()
{
    qApp->setStyleSheet(QStringLiteral(R"(
        QMainWindow, QWidget {
            background: #101419;
            color: #e8edf4;
            font-size: 14px;
        }
        QWidget#Panel, QWidget > QWidget:first-child {
            background: #151b23;
            border: 1px solid #26313d;
            border-radius: 10px;
        }
        QLabel#Heading {
            font-size: 24px;
            font-weight: 700;
        }
        QLabel#PanelTitle {
            font-size: 17px;
            font-weight: 700;
        }
        QLabel#Muted {
            color: #a8b3c2;
        }
        QLabel#Badge {
            background: #0f3935;
            color: #7ee0d1;
            border-radius: 14px;
            padding: 6px 12px;
            font-weight: 700;
        }
        QLabel#Preview {
            background: #102724;
            border: 1px solid #1d4f49;
            border-radius: 8px;
            padding: 12px;
        }
        QLabel#ErrorText {
            color: #ffb4a8;
        }
        QListWidget, QTextEdit, QLineEdit {
            background: #0f141a;
            border: 1px solid #26313d;
            border-radius: 8px;
            padding: 8px;
            selection-background-color: #0f766e;
        }
        QListWidget::item {
            padding: 12px;
            border-radius: 8px;
            margin: 4px;
        }
        QListWidget::item:selected {
            background: #0f3935;
        }
        QPushButton {
            background: #0f766e;
            border: 0;
            border-radius: 8px;
            padding: 10px 16px;
            font-weight: 600;
        }
        QPushButton:hover {
            background: #128579;
        }
        QPushButton:disabled {
            background: #303842;
            color: #7c8796;
        }
        QProgressBar {
            border: 1px solid #26313d;
            border-radius: 5px;
            height: 10px;
            text-align: center;
        }
        QProgressBar::chunk {
            background: #16a394;
            border-radius: 5px;
        }
    )"));
}

void MainWindow::refreshDisks()
{
    setBusy(true);
    QString error;
    m_disks = m_enumerator.listUsbDisks(&error);
    setBusy(false);
    if (!error.isEmpty()) {
        QMessageBox::critical(this, QStringLiteral("Unable to scan USB disks"), error);
    }
    renderDisks();
}

void MainWindow::renderDisks()
{
    m_diskList->clear();
    for (const DiskInfo &disk : m_disks) {
        const QString letters = disk.driveLetters.isEmpty() ? QStringLiteral("No drive letter") : disk.driveLetters.join(QStringLiteral(", "));
        const QString text = QStringLiteral("Disk %1 - %2\n%3 | %4 | %5")
                                 .arg(disk.number)
                                 .arg(disk.name.isEmpty() ? QStringLiteral("USB disk") : disk.name)
                                 .arg(formatByteSize(disk.size), disk.partitionStyle, letters);
        m_diskList->addItem(text);
    }
    if (m_disks.isEmpty()) {
        m_diskList->addItem(QStringLiteral("No USB disks found."));
    }
    selectDisk(m_diskList->currentRow());
}

void MainWindow::selectDisk(int row)
{
    if (row < 0 || row >= m_disks.size()) {
        m_title->setText(QStringLiteral("Select a USB disk"));
        m_details->setText(QStringLiteral("Only USB disks reported by Windows are shown."));
        m_confirmation->clear();
        updateRestoreState();
        return;
    }

    const DiskInfo &disk = m_disks[row];
    m_title->setText(QStringLiteral("Disk %1 - %2").arg(disk.number).arg(disk.name));
    m_details->setText(QStringLiteral("Size: %1\nCurrent layout: %2\nDrive letters: %3\nHealth: %4")
                           .arg(formatByteSize(disk.size))
                           .arg(disk.partitionStyle)
                           .arg(disk.driveLetters.isEmpty() ? QStringLiteral("None") : disk.driveLetters.join(QStringLiteral(", ")))
                           .arg(disk.health.isEmpty() ? QStringLiteral("Unknown") : disk.health));
    m_confirmation->setPlaceholderText(QStringLiteral("Type %1").arg(confirmationPhrase(disk.number)));
    updateRestoreState();
}

void MainWindow::updateRestoreState()
{
    DiskInfo *disk = selectedDisk();
    if (!disk) {
        m_validation->clear();
        m_restoreButton->setEnabled(false);
        return;
    }

    QString reason;
    if (!isSafeRestoreTarget(*disk, &reason)) {
        m_validation->setText(reason);
        m_restoreButton->setEnabled(false);
        return;
    }

    if (m_confirmation->text() != confirmationPhrase(disk->number)) {
        m_validation->setText(QStringLiteral("Type %1 to enable restore.").arg(confirmationPhrase(disk->number)));
        m_restoreButton->setEnabled(false);
        return;
    }

    m_validation->setText(largeRestoreTargetWarning(*disk));
    m_restoreButton->setEnabled(true);
}

void MainWindow::startRestore()
{
    DiskInfo *disk = selectedDisk();
    if (!disk) {
        return;
    }

    if (isLargeRestoreTarget(*disk)) {
        const QMessageBox::StandardButton result = QMessageBox::warning(
            this,
            QStringLiteral("Large USB disk"),
            largeRestoreTargetWarning(*disk) + QStringLiteral("\n\nRestoring will erase every partition and file on this disk."),
            QMessageBox::Cancel | QMessageBox::Yes,
            QMessageBox::Cancel);
        if (result != QMessageBox::Yes) {
            return;
        }
    }

    setBusy(true);
    m_restoreButton->setEnabled(false);
    appendLog(QStringLiteral("Starting restore for disk %1").arg(disk->number));

    auto *thread = new QThread(this);
    auto *worker = new RestoreWorker(*disk);
    worker->moveToThread(thread);
    m_restoreThread = thread;

    connect(thread, &QThread::started, worker, &RestoreWorker::run);
    connect(worker, &RestoreWorker::progress, this, &MainWindow::onRestoreProgress);
    connect(worker, &RestoreWorker::logMessage, &m_logger, &Logger::log, Qt::QueuedConnection);
    connect(worker, &RestoreWorker::logFileOnly, &m_logger, &Logger::logFileOnly, Qt::QueuedConnection);
    connect(worker, &RestoreWorker::failed, this, &MainWindow::onRestoreFailed);
    connect(worker, &RestoreWorker::finished, this, &MainWindow::onRestoreFinished);
    connect(worker, &RestoreWorker::failed, thread, &QThread::quit);
    connect(worker, &RestoreWorker::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, this, [this, thread]() {
        if (m_restoreThread == thread) {
            m_restoreThread = nullptr;
        }
    });
    connect(thread, &QThread::finished, worker, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

void MainWindow::onRestoreProgress(const QString &message)
{
    Q_UNUSED(message);
}

void MainWindow::onRestoreFailed(const QString &message)
{
    setBusy(false);
    QMessageBox::critical(this, QStringLiteral("Restore failed"), message + QStringLiteral("\n\nLog file:\n%1").arg(m_logger.path()));
    refreshDisks();
}

void MainWindow::onRestoreFinished(const QString &driveRoot)
{
    setBusy(false);
    QMessageBox::information(this, QStringLiteral("Restore complete"), QStringLiteral("USB restored as %1").arg(driveRoot));
    m_confirmation->clear();
    refreshDisks();
}

void MainWindow::setBusy(bool busy)
{
    m_refreshButton->setEnabled(!busy);
    m_diskList->setEnabled(!busy);
    m_confirmation->setEnabled(!busy);
    if (busy) {
        m_progress->show();
    } else {
        m_progress->hide();
    }
}

void MainWindow::appendLog(const QString &message)
{
    m_log->append(message);
}

DiskInfo *MainWindow::selectedDisk()
{
    const int row = m_diskList->currentRow();
    if (row < 0 || row >= m_disks.size()) {
        return nullptr;
    }
    return &m_disks[row];
}

}

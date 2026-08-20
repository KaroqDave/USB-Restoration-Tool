#pragma once

#include "core/disk.h"
#include "gui/app_settings.h"
#include "gui/theme.h"
#include "platform/disk_service.h"
#include "platform/logger.h"

#include <QElapsedTimer>
#include <QMainWindow>
#include <QPointer>
#include <QVector>

class QCheckBox;
class QCloseEvent;
class QComboBox;
class QDialog;
class QLabel;
class QLayout;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QThread;
class QTimer;

namespace usbrestore {

class RestoreWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(DiskService &service, Theme initialTheme, QWidget *parent = nullptr);

  protected:
    void closeEvent(QCloseEvent *event) override;

  private slots:
    void refreshDisks();
    void onSelectionChanged();
    void updateRestoreState();
    void startRestore();
    void cancelRestore();
    void onRestoreProgress(int step, int totalSteps, const QString &message);
    void onRestoreFailed(const QString &message);
    void onRestoreCancelled();
    void onRestoreFinished(const QString &location);
    void onStallTick();
    void toggleTheme();
    void showAbout();
    void showActivity();
    void openLogFile();

  private:
    void buildUi();
    QLayout *buildHeaderLayout();
    QWidget *buildDiskSection();
    QWidget *buildDetailSection();
    QWidget *buildRestoreSection();
    QLayout *buildActionLayout();
    QWidget *buildFooterWarning();
    QDialog *activityDialog();

    void loadSettings();
    void saveSettings();
    void applyCurrentTheme();
    void refreshStatusBadge();

    void renderDisks();
    void renderSelectedDisk();
    void setStatus(StatusKind status, const QString &message, const QString &detail = {});
    void setRunning(bool running);
    void noteRestoreActivity();
    void appendLog(const QString &message);
    bool confirmErase(const DiskInfo &disk, PartitionStyle style, FileSystemType fileSystem);
    PartitionStyle selectedPartitionStyle() const;
    FileSystemType selectedFileSystem() const;
    const DiskInfo *selectedDisk() const;
    bool waitForRestoreThread(int timeoutMs = 30000);

    DiskService &m_service;
    QVector<DiskInfo> m_disks;
    RestoreGuard m_guard;
    Logger m_logger;
    AppSettings m_settings;
    Theme m_theme = Theme::System;
    StatusKind m_status = StatusKind::Info;
    bool m_restoreRunning = false;

    // The stall watch: how long the backend has been silent on the current
    // step, so a stick stuck in uninterruptible sleep looks different from a
    // hung program.
    QElapsedTimer m_restoreActivity;
    int m_restoreStep = 0;
    QString m_restoreStepText;
    bool m_stallNoticeShown = false;

    // What the running restore was asked to produce. The completion message
    // reports this rather than re-reading the combo box, which is re-enabled
    // (and in principle changeable) before the message is built.
    FileSystemType m_restoreFileSystem = FileSystemType::ExFat;

    QListWidget *m_diskList = nullptr;
    QLabel *m_diskCount = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailValues = nullptr;
    QLabel *m_planLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QComboBox *m_fileSystemCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_themeButton = nullptr;
    QPushButton *m_activityButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QTimer *m_stallTimer = nullptr;
    QTextEdit *m_log = nullptr;
    QPointer<QDialog> m_activityDialog;
    QPointer<QThread> m_restoreThread;
    QPointer<RestoreWorker> m_restoreWorker;
};

} // namespace usbrestore

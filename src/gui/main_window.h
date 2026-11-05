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

class QCloseEvent;
class QComboBox;
class QDialog;
class QEvent;
class QGroupBox;
class QLabel;
class QLayout;
class QListWidget;
class QPushButton;
class QTextEdit;
class QThread;
class QTimer;

namespace usbrestore {

class RestoreWorker;
class RestoreDialog;
class Translations;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(
        DiskService &service, Theme initialTheme, Translations *translations = nullptr, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool isScanning() const { return m_scanning; }

  signals:
    void scanFinished();

  protected:
    void changeEvent(QEvent *event) override;
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
    void cycleLanguage();
    void showAbout();
    void showActivity();
    void openLogFile();

  private:
    void buildUi();
    QLayout *buildHeaderLayout();
    QWidget *buildDiskSection();
    QWidget *buildDetailSection();
    QWidget *buildRestoreSection();
    QWidget *buildFooterWarning();
    QDialog *activityDialog();

    void loadSettings();
    void saveSettings();
    void applyCurrentTheme();
    void retranslateUi();
    void applyTranslatedText();
    void applyActivityDialogText();
    void fitMinimumWidthToContent();
    void refreshStatusBadge();

    void applyScan(QVector<DiskInfo> disks, const RestoreMessage &error, const RestoreGuard &guard);
    void renderDisks();
    void renderSelectedDisk();
    void setStatus(StatusKind status, const QString &message, const QString &detail = {});
    void setRunning(bool running);
    void noteRestoreActivity();
    void appendLog(const QString &message);
    void beginRestore(const RestoreRequest &request);
    PartitionStyle selectedPartitionStyle() const;
    FileSystemType selectedFileSystem() const;
    const DiskInfo *selectedDisk() const;
    bool waitForRestoreThread(int timeoutMs = 30000);

    DiskService &m_service;
    Translations *m_translations = nullptr;
    QVector<DiskInfo> m_disks;
    RestoreMessage m_scanError;
    RestoreGuard m_guard;
    Logger m_logger;
    AppSettings m_settings;
    Theme m_theme = Theme::System;
    StatusKind m_status = StatusKind::Info;
    bool m_restoreRunning = false;
    // The scan runs off the GUI thread; Refresh and Restore wait for it.
    bool m_scanning = false;

    // Stall watch: tells a stick stuck in uninterruptible sleep apart from a hung program.
    QElapsedTimer m_restoreActivity;
    int m_restoreStep = 0;
    bool m_stallNoticeShown = false;

    // The result describes the confirmed request, not a later selection.
    FileSystemType m_restoreFileSystem = FileSystemType::ExFat;

    QListWidget *m_diskList = nullptr;
    QLabel *m_subtitle = nullptr;
    QLabel *m_privilegeBadge = nullptr;
    QLabel *m_diskCount = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailValues = nullptr;
    QLabel *m_planLabel = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLabel *m_partitionLabel = nullptr;
    QLabel *m_filesystemLabel = nullptr;
    QLabel *m_warningTitle = nullptr;
    QLabel *m_warningDetail = nullptr;
    QLabel *m_activityCaption = nullptr;
    QGroupBox *m_diskCard = nullptr;
    QGroupBox *m_detailCard = nullptr;
    QGroupBox *m_restoreCard = nullptr;
    QComboBox *m_styleCombo = nullptr;
    QComboBox *m_fileSystemCombo = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_themeButton = nullptr;
    QPushButton *m_languageButton = nullptr;
    QPushButton *m_activityButton = nullptr;
    QPushButton *m_aboutButton = nullptr;
    QPushButton *m_openLogButton = nullptr;
    QPushButton *m_activityCloseButton = nullptr;
    QTimer *m_stallTimer = nullptr;
    QTextEdit *m_log = nullptr;
    QPointer<QDialog> m_activityDialog;
    QPointer<RestoreDialog> m_restoreDialog;
    QPointer<QThread> m_scanThread;
    QPointer<QThread> m_restoreThread;
    QPointer<RestoreWorker> m_restoreWorker;
};

} // namespace usbrestore

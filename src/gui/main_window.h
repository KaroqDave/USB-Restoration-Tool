#pragma once

#include "core/disk.h"
#include "gui/app_settings.h"
#include "gui/theme.h"
#include "win/disk_enumerator.h"
#include "win/logger.h"

#include <QMainWindow>
#include <QPointer>
#include <QStringList>
#include <QVector>

class QCloseEvent;
class QGroupBox;
class QLabel;
class QLayout;
class QLineEdit;
class QListWidget;
class QProgressBar;
class QPushButton;
class QTextEdit;
class QThread;

namespace usbrestore {

class RestoreWorker;

class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    explicit MainWindow(Theme initialTheme, QWidget *parent = nullptr);

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
    void onRestoreFinished(const QString &driveRoot);
    void toggleTheme();
    void showAbout();
    void openLogFile();

  private:
    void buildUi();
    QLayout *buildHeaderLayout();
    QWidget *buildDiskSection();
    QWidget *buildDetailSection();
    QWidget *buildConfirmSection();
    QLayout *buildActionLayout();
    QWidget *buildLogSection();
    QWidget *buildFooterWarning();

    void loadSettings();
    void saveSettings();
    void applyCurrentTheme();
    void refreshStatusBadge();

    void renderDisks();
    void renderSelectedDisk();
    void setStatus(StatusKind status, const QString &message, const QString &detail = {});
    void setRunning(bool running);
    void appendLog(const QString &message);
    const DiskInfo *selectedDisk() const;
    bool waitForRestoreThread(int timeoutMs = 30000);

    DiskEnumerator m_enumerator;
    QVector<DiskInfo> m_disks;
    QStringList m_protectedDriveLetters;
    Logger m_logger;
    AppSettings m_settings;
    Theme m_theme = Theme::System;
    StatusKind m_status = StatusKind::Info;
    bool m_restoreRunning = false;

    QListWidget *m_diskList = nullptr;
    QLabel *m_diskCount = nullptr;
    QLabel *m_detailTitle = nullptr;
    QLabel *m_detailValues = nullptr;
    QLabel *m_planLabel = nullptr;
    QLabel *m_confirmHint = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_detailLabel = nullptr;
    QLineEdit *m_confirmation = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_themeButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QTextEdit *m_log = nullptr;
    QPointer<QThread> m_restoreThread;
    QPointer<RestoreWorker> m_restoreWorker;
};

} // namespace usbrestore

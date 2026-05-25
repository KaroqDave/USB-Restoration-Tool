#pragma once

#include "win/Core.h"
#include "win/DiskEnumerator.h"
#include "win/Logger.h"

#include <QMainWindow>
#include <QPointer>
#include <QVector>

class QLabel;
class QListWidget;
class QListWidgetItem;
class QCloseEvent;
class QLineEdit;
class QPushButton;
class QProgressBar;
class QTextEdit;
class QThread;

namespace usbrestore {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget *parent = nullptr);

protected:
    void closeEvent(QCloseEvent *event) override;

private slots:
    void refreshDisks();
    void selectDisk(int row);
    void updateRestoreState();
    void startRestore();
    void onRestoreProgress(const QString &message);
    void onRestoreFailed(const QString &message);
    void onRestoreFinished(const QString &driveRoot);

private:
    void buildUi();
    void applyStyle();
    void setBusy(bool busy);
    void renderDisks();
    void appendLog(const QString &message);
    DiskInfo *selectedDisk();

    DiskEnumerator m_enumerator;
    QVector<DiskInfo> m_disks;
    Logger m_logger;

    QListWidget *m_diskList = nullptr;
    QLabel *m_title = nullptr;
    QLabel *m_details = nullptr;
    QLabel *m_preview = nullptr;
    QLabel *m_validation = nullptr;
    QLineEdit *m_confirmation = nullptr;
    QPushButton *m_restoreButton = nullptr;
    QPushButton *m_refreshButton = nullptr;
    QProgressBar *m_progress = nullptr;
    QTextEdit *m_log = nullptr;
    QPointer<QThread> m_restoreThread;
};

}

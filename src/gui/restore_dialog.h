#pragma once

#include "platform/disk_service.h"

#include <QDialog>
#include <QElapsedTimer>
#include <QVector>

class QCheckBox;
class QLabel;
class QProgressBar;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTimer;

namespace usbrestore {

class RestoreStepIndicator;

class RestoreDialog : public QDialog {
    Q_OBJECT
  public:
    RestoreDialog(const RestoreRequest &request, const QVector<RestoreStep> &plan, QWidget *parent = nullptr);

    void setProgress(int step, int totalSteps, const QString &message);
    void setNotice(const QString &message);
    void setCancelling();
    void finishSuccess(const QString &message);
    void finishFailure(const QString &message);
    void finishCancelled(const QString &message);

  signals:
    void restoreConfirmed();
    void cancelRequested();
    void openLogRequested();

  public slots:
    void done(int result) override;

  protected:
    void resizeEvent(QResizeEvent *event) override;

  private:
    enum class Stage { Confirmation, Running, Finished };
    struct StepRow {
        QWidget *widget;
        RestoreStepIndicator *indicator;
        QLabel *state;
    };

    void updateClock();
    void confirmRestore();
    void finish(const QString &title, const QString &message, const QString &outcome);
    void completeCurrentStep();
    void updateMessageGeometry();

    QVector<StepRow> m_rows;
    int m_step = 0;
    int m_firstDestructiveStep;
    Stage m_stage = Stage::Confirmation;
    bool m_cancelling = false;
    QElapsedTimer m_elapsed;
    qint64 m_stepStartedMs = 0;
    QLabel *m_title = nullptr;
    QLabel *m_eyebrow = nullptr;
    QLabel *m_message = nullptr;
    QScrollArea *m_messageArea = nullptr;
    QLabel *m_counter = nullptr;
    QLabel *m_clock = nullptr;
    QLabel *m_notice = nullptr;
    RestoreStepIndicator *m_indicator = nullptr;
    QProgressBar *m_progress = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPushButton *m_eraseButton = nullptr;
    QPushButton *m_logButton = nullptr;
    QCheckBox *m_acknowledgement = nullptr;
    QStackedWidget *m_pages = nullptr;
    QPushButton *m_doneButton = nullptr;
    QScrollArea *m_steps = nullptr;
    QTimer *m_timer = nullptr;
};

} // namespace usbrestore

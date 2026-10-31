#include "gui/restore_dialog.h"

#include "gui/theme.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QHBoxLayout>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QProgressBar>
#include <QPushButton>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStyle>
#include <QTimer>
#include <QVBoxLayout>

namespace usbrestore {

namespace {

QString stepTitle(RestoreStep step)
{
    switch (step) {
    case RestoreStep::VerifyDisk:
        return QStringLiteral("Check drive safety");
    case RestoreStep::IdentifyDevice:
        return QStringLiteral("Confirm drive identity");
    case RestoreStep::Unmount:
        return QStringLiteral("Unmount existing volumes");
    case RestoreStep::OpenExclusive:
        return QStringLiteral("Claim exclusive access");
    case RestoreStep::OpenDrive:
        return QStringLiteral("Open the drive");
    case RestoreStep::RefreshMetadata:
        return QStringLiteral("Refresh drive information");
    case RestoreStep::DetachDriveLetters:
        return QStringLiteral("Disconnect existing volumes");
    case RestoreStep::DeletePartitions:
        return QStringLiteral("Remove old partitions");
    case RestoreStep::LockDrive:
        return QStringLiteral("Lock the drive");
    case RestoreStep::ClearSignatures:
        return QStringLiteral("Clear old disk signatures");
    case RestoreStep::ResetToRaw:
        return QStringLiteral("Reset the disk layout");
    case RestoreStep::WritePartitionTable:
        return QStringLiteral("Write the partition table");
    case RestoreStep::CreatePartition:
        return QStringLiteral("Create the new partition");
    case RestoreStep::RereadPartitionTable:
        return QStringLiteral("Reload the partition table");
    case RestoreStep::WaitForPartition:
        return QStringLiteral("Wait for the new partition");
    case RestoreStep::WaitForVolume:
        return QStringLiteral("Wait for the new volume");
    case RestoreStep::RefreshNewVolume:
        return QStringLiteral("Refresh the new volume");
    case RestoreStep::AssignDriveLetter:
        return QStringLiteral("Assign a drive letter");
    case RestoreStep::Format:
        return QStringLiteral("Format the filesystem");
    case RestoreStep::Complete:
        return QStringLiteral("Finish the restore");
    }
    return QStringLiteral("Restore step");
}

QLabel *label(const QString &text, const char *name)
{
    auto *widget = new QLabel(text);
    widget->setObjectName(QString::fromLatin1(name));
    widget->setTextFormat(Qt::PlainText);
    widget->setWordWrap(true);
    return widget;
}

void setState(QWidget *widget, const QString &state)
{
    widget->setProperty("state", state);
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
    for (auto *child : widget->findChildren<QLabel *>()) {
        child->style()->unpolish(child);
        child->style()->polish(child);
    }
    widget->update();
}

} // namespace

class RestoreStepIndicator : public QWidget {
  public:
    explicit RestoreStepIndicator(int size, QWidget *parent = nullptr) : QWidget(parent)
    {
        setFixedSize(size, size);
        setObjectName(QStringLiteral("restoreStepIndicator"));
        setState(this, QStringLiteral("pending"));
        setAttribute(Qt::WA_TransparentForMouseEvents);
    }

    void setPhase(int phase)
    {
        m_phase = phase;
        update();
    }

  protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter painter(this);
        painter.setRenderHint(QPainter::Antialiasing);
        const QString state = property("state").toString();
        const bool large = width() > 40;
        const qreal stroke = large ? 4.0 : 2.0;
        const QRectF circle = QRectF(rect()).adjusted(stroke + 1, stroke + 1, -stroke - 1, -stroke - 1);
        const Palette &colors = appliedPalette();
        const QColor color = state == QStringLiteral("pending") ? colors.mutedText : colors.accent;
        painter.setPen(QPen(colors.border, stroke));
        painter.drawEllipse(circle);
        painter.setPen(QPen(color, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (state == QStringLiteral("active")) {
            painter.drawArc(circle, -m_phase * 16, 100 * 16);
        } else if (state == QStringLiteral("complete")) {
            painter.drawEllipse(circle);
            QPainterPath check;
            check.moveTo(width() * 0.29, height() * 0.51);
            check.lineTo(width() * 0.44, height() * 0.65);
            check.lineTo(width() * 0.71, height() * 0.36);
            painter.drawPath(check);
        } else if (state == QStringLiteral("confirmation")) {
            painter.setPen(QPen(colors.statusError, stroke, Qt::SolidLine, Qt::RoundCap));
            painter.drawEllipse(circle);
            painter.drawLine(QPointF(width() * 0.5, height() * 0.28), QPointF(width() * 0.5, height() * 0.55));
            painter.drawPoint(QPointF(width() * 0.5, height() * 0.71));
        } else if (state == QStringLiteral("error")) {
            painter.setPen(QPen(colors.statusError, stroke, Qt::SolidLine, Qt::RoundCap));
            painter.drawEllipse(circle);
            painter.drawLine(QPointF(width() * 0.37, height() * 0.37), QPointF(width() * 0.63, height() * 0.63));
            painter.drawLine(QPointF(width() * 0.63, height() * 0.37), QPointF(width() * 0.37, height() * 0.63));
        } else if (state == QStringLiteral("cancelled")) {
            painter.drawLine(QPointF(width() * 0.33, height() * 0.5), QPointF(width() * 0.67, height() * 0.5));
        }
    }

  private:
    int m_phase = 0;
};

RestoreDialog::RestoreDialog(const RestoreRequest &request, const QVector<RestoreStep> &plan, QWidget *parent)
    : QDialog(parent), m_firstDestructiveStep(firstDestructiveStepOf(plan))
{
    const DiskInfo &disk = request.disk;
    setObjectName(QStringLiteral("restoreDialog"));
    setWindowTitle(QStringLiteral("Confirm USB restore"));
    setWindowModality(Qt::WindowModal);
    setWindowFlag(Qt::WindowContextHelpButtonHint, false);
    setMinimumSize(480, 520);
    const QScreen *targetScreen = parent ? parent->screen() : QApplication::primaryScreen();
    const QSize available = targetScreen ? targetScreen->availableGeometry().size() - QSize(48, 80) : QSize(620, 720);
    resize(qMin(620, available.width()), qMin(720, available.height()));

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(16);

    auto *hero = new QHBoxLayout;
    hero->setSpacing(18);
    m_indicator = new RestoreStepIndicator(64, this);
    setState(m_indicator, QStringLiteral("confirmation"));
    hero->addWidget(m_indicator);
    auto *headings = new QVBoxLayout;
    headings->setSpacing(4);
    m_eyebrow = label(QStringLiteral("CHECK BEFORE ERASING"), "restoreEyebrow");
    headings->addWidget(m_eyebrow);
    m_title = label(QStringLiteral("Erase this USB drive?"), "restoreTitle");
    headings->addWidget(m_title);
    hero->addLayout(headings, 1);
    layout->addLayout(hero);

    auto *target = new QFrame;
    target->setObjectName(QStringLiteral("restoreTarget"));
    auto *targetLayout = new QVBoxLayout(target);
    targetLayout->setContentsMargins(14, 12, 14, 12);
    targetLayout->setSpacing(4);
    targetLayout->addWidget(label(describeDisk(disk), "detailTitle"));
    targetLayout->addWidget(label(
        QStringLiteral("%1  /  %2  /  Label: %3")
            .arg(partitionStyleLabel(request.style), fileSystemTypeName(request.fileSystem), request.volumeLabel),
        "footnote"));
    layout->addWidget(target);

    m_pages = new QStackedWidget;
    auto *confirmationPage = new QWidget;
    auto *confirmationLayout = new QVBoxLayout(confirmationPage);
    confirmationLayout->setContentsMargins(0, 0, 0, 0);
    confirmationLayout->setSpacing(16);
    auto *summaryArea = new QScrollArea;
    summaryArea->setWidgetResizable(true);
    summaryArea->setFrameShape(QFrame::NoFrame);
    summaryArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *summary = new QWidget;
    auto *summaryLayout = new QVBoxLayout(summary);
    summaryLayout->setContentsMargins(0, 0, 8, 0);
    summaryLayout->setSpacing(14);
    auto *warning = new QFrame;
    warning->setObjectName(QStringLiteral("warningBanner"));
    auto *warningLayout = new QVBoxLayout(warning);
    warningLayout->setContentsMargins(14, 12, 14, 12);
    warningLayout->setSpacing(6);
    warningLayout->addWidget(label(QStringLiteral("This cannot be undone."), "warningTitle"));
    warningLayout->addWidget(
        label(QStringLiteral("Every partition and file on this drive will be permanently erased."), "eraseWarning"));
    summaryLayout->addWidget(warning);
    summaryLayout->addWidget(label(
        QStringLiteral("Creates one %1 partition formatted %2 and labelled %3.")
            .arg(partitionStyleLabel(request.style), fileSystemTypeName(request.fileSystem), request.volumeLabel),
        "erasePlan"));
    const QString mounts =
        disk.mountPoints.isEmpty() ? QStringLiteral("—") : disk.mountPoints.join(QStringLiteral(", "));
    const QString labels = disk.labels.isEmpty() ? QStringLiteral("—") : disk.labels.join(QStringLiteral(", "));
    auto *contents = label(QStringLiteral("Mounted at: %1\nVolume labels: %2").arg(mounts, labels), "eraseContents");
    contents->setTextInteractionFlags(Qt::TextSelectableByMouse);
    summaryLayout->addWidget(contents);
    const QString largeWarning = largeRestoreTargetWarning(disk);
    if (!largeWarning.isEmpty()) {
        summaryLayout->addWidget(label(largeWarning, "warningTitle"));
    }
    summaryLayout->addStretch();
    summaryArea->setWidget(summary);
    confirmationLayout->addWidget(summaryArea, 1);
    const QString diskId = disk.displayId.isEmpty() ? disk.deviceId : disk.displayId;
    m_acknowledgement =
        new QCheckBox(QStringLiteral("I understand that everything on %1\nwill be permanently erased.").arg(diskId));
    m_acknowledgement->setObjectName(QStringLiteral("eraseAcknowledgement"));
    confirmationLayout->addWidget(m_acknowledgement);
    m_pages->addWidget(confirmationPage);

    auto *progressPage = new QWidget;
    auto *progressLayout = new QVBoxLayout(progressPage);
    progressLayout->setContentsMargins(0, 0, 0, 0);
    progressLayout->setSpacing(16);
    m_message = label(
        QStringLiteral("Preparing the restore. Approve the system permission prompt if asked."), "restoreMessage");
    m_messageArea = new QScrollArea;
    m_messageArea->setWidgetResizable(true);
    m_messageArea->setFrameShape(QFrame::NoFrame);
    m_messageArea->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_messageArea->setFixedHeight(48);
    m_messageArea->setWidget(m_message);
    progressLayout->addWidget(m_messageArea);

    auto *metrics = new QHBoxLayout;
    m_counter = label(QStringLiteral("Preparing · %1 steps").arg(plan.size()), "restoreCounter");
    m_clock = label(QStringLiteral("Elapsed 0:00"), "restoreClock");
    m_clock->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    metrics->addWidget(m_counter, 1);
    metrics->addWidget(m_clock);
    progressLayout->addLayout(metrics);
    m_progress = new QProgressBar;
    m_progress->setObjectName(QStringLiteral("restoreStepProgress"));
    m_progress->setAccessibleName(QStringLiteral("Completed restore steps, not a time estimate"));
    m_progress->setRange(0, static_cast<int>(plan.size()));
    m_progress->setValue(0);
    m_progress->setTextVisible(false);
    progressLayout->addWidget(m_progress);

    m_steps = new QScrollArea;
    m_steps->setObjectName(QStringLiteral("restoreSteps"));
    m_steps->setWidgetResizable(true);
    m_steps->setFrameShape(QFrame::NoFrame);
    m_steps->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto *steps = new QWidget;
    auto *stepsLayout = new QVBoxLayout(steps);
    stepsLayout->setContentsMargins(0, 0, 8, 0);
    stepsLayout->setSpacing(4);
    for (int index = 0; index < plan.size(); ++index) {
        auto *row = new QFrame;
        row->setObjectName(QStringLiteral("restoreStepRow"));
        row->setProperty("step", index + 1);
        setState(row, QStringLiteral("pending"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 8, 10, 8);
        rowLayout->setSpacing(10);
        auto *indicator = new RestoreStepIndicator(24, row);
        auto *title = label(stepTitle(plan.at(index)), "restoreStepTitle");
        auto *state = label(QStringLiteral("Waiting"), "restoreStepState");
        state->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        state->setMinimumWidth(88);
        rowLayout->addWidget(indicator);
        rowLayout->addWidget(title, 1);
        rowLayout->addWidget(state);
        m_rows.append({row, indicator, state});
        stepsLayout->addWidget(row);
    }
    stepsLayout->addStretch();
    m_steps->setWidget(steps);
    progressLayout->addWidget(m_steps, 1);

    m_notice = label(
        QStringLiteral("Keep the drive connected. You can request cancellation before writing begins."),
        "restoreNotice");
    m_notice->setAccessibleName(QStringLiteral("Restore safety information"));
    progressLayout->addWidget(m_notice);
    m_pages->addWidget(progressPage);
    layout->addWidget(m_pages, 1);
    auto *buttons = new QHBoxLayout;
    m_logButton = new QPushButton(QStringLiteral("Open log file"));
    m_logButton->setProperty("variant", "text");
    m_logButton->setAutoDefault(false);
    m_logButton->hide();
    connect(m_logButton, &QPushButton::clicked, this, &RestoreDialog::openLogRequested);
    buttons->addWidget(m_logButton);
    buttons->addStretch();
    m_cancelButton = new QPushButton(QStringLiteral("Cancel"));
    m_cancelButton->setObjectName(QStringLiteral("cancelRestore"));
    m_cancelButton->setAutoDefault(false);
    m_cancelButton->setDefault(true);
    connect(m_cancelButton, &QPushButton::clicked, this, [this]() {
        if (m_stage == Stage::Confirmation) {
            reject();
        } else if (m_stage == Stage::Running) {
            emit cancelRequested();
        }
    });
    buttons->addWidget(m_cancelButton);
    m_eraseButton = new QPushButton(QStringLiteral("Erase and restore"));
    m_eraseButton->setObjectName(QStringLiteral("confirmErase"));
    m_eraseButton->setProperty("variant", "danger");
    m_eraseButton->setAutoDefault(false);
    m_eraseButton->setEnabled(false);
    connect(m_acknowledgement, &QCheckBox::toggled, this, [this](bool checked) {
        m_eraseButton->setEnabled(m_stage == Stage::Confirmation && checked);
    });
    connect(m_eraseButton, &QPushButton::clicked, this, &RestoreDialog::confirmRestore);
    buttons->addWidget(m_eraseButton);
    m_doneButton = new QPushButton(QStringLiteral("Done"));
    m_doneButton->setObjectName(QStringLiteral("restoreDone"));
    m_doneButton->setProperty("variant", "primary");
    m_doneButton->hide();
    connect(m_doneButton, &QPushButton::clicked, this, &QDialog::accept);
    buttons->addWidget(m_doneButton);
    layout->addLayout(buttons);

    m_timer = new QTimer(this);
    const bool animate = QApplication::style()->styleHint(QStyle::SH_Widget_Animate);
    m_timer->setInterval(animate ? 40 : 1000);
    connect(m_timer, &QTimer::timeout, this, [this, animate]() {
        updateClock();
        if (animate) {
            const int phase = static_cast<int>((m_elapsed.elapsed() % 1200) * 360 / 1200);
            m_indicator->setPhase(phase);
            if (m_step > 0) {
                m_rows[m_step - 1].indicator->setPhase(phase);
            }
        }
    });
}

void RestoreDialog::confirmRestore()
{
    // The acknowledgement and the explicit destructive action must name the same frozen request.
    if (m_stage != Stage::Confirmation || !m_acknowledgement->isChecked()) {
        return;
    }
    m_stage = Stage::Running;
    m_eraseButton->setEnabled(false);
    m_eraseButton->hide();
    m_acknowledgement->setEnabled(false);
    m_cancelButton->setDefault(false);
    m_cancelButton->setText(QStringLiteral("Cancel restore"));
    m_logButton->show();
    m_pages->setCurrentIndex(1);
    setWindowTitle(QStringLiteral("Restoring USB drive"));
    m_eyebrow->setText(QStringLiteral("A FRESH START"));
    m_title->setText(QStringLiteral("Restoring your USB"));
    setState(m_indicator, QStringLiteral("active"));
    updateMessageGeometry();
    m_elapsed.start();
    m_timer->start();
    m_cancelButton->setFocus();
    emit restoreConfirmed();
}

void RestoreDialog::updateClock()
{
    const qint64 seconds = m_elapsed.elapsed() / 1000;
    m_clock->setText(QStringLiteral("Elapsed %1:%2").arg(seconds / 60).arg(seconds % 60, 2, 10, QLatin1Char('0')));
}

void RestoreDialog::completeCurrentStep()
{
    if (m_step == 0) {
        return;
    }
    auto &row = m_rows[m_step - 1];
    setState(row.widget, QStringLiteral("complete"));
    setState(row.indicator, QStringLiteral("complete"));
    row.state->setText(QStringLiteral("Done · %1 s").arg((m_elapsed.elapsed() - m_stepStartedMs) / 1000.0, 0, 'f', 1));
}

void RestoreDialog::setProgress(int step, int totalSteps, const QString &message)
{
    if (m_stage != Stage::Running || totalSteps != m_rows.size() || step != m_step + 1 || step > m_rows.size()) {
        return;
    }
    completeCurrentStep();
    m_step = step;
    m_stepStartedMs = m_elapsed.elapsed();
    auto &row = m_rows[step - 1];
    setState(row.widget, QStringLiteral("active"));
    setState(row.indicator, QStringLiteral("active"));
    row.state->setText(QStringLiteral("In progress"));
    m_message->setText(message);
    updateMessageGeometry();
    m_counter->setText(QStringLiteral("Step %1 of %2").arg(step).arg(totalSteps));
    // A step signal starts work; even the last step is not success until the worker finishes.
    m_progress->setValue(step - 1);
    m_steps->ensureWidgetVisible(row.widget, 0, 20);
    if (step >= m_firstDestructiveStep) {
        m_cancelButton->setEnabled(false);
        m_cancelButton->setText(QStringLiteral("Writing in progress"));
        m_notice->setText(QStringLiteral("The disk is being rewritten. Keep it connected and wait for the result."));
    } else if (!m_cancelling) {
        m_notice->setText(QStringLiteral("Keep the drive connected. Cancellation stops at the next safe checkpoint."));
    }
}

void RestoreDialog::setNotice(const QString &message)
{
    // Stall and recovery text must not replace the cancellation sentence:
    // later progress updates leave it in place while cancellation is pending.
    if (m_stage == Stage::Running && !m_cancelling) {
        m_notice->setText(message);
    }
}

void RestoreDialog::setCancelling()
{
    if (m_stage != Stage::Running || m_step >= m_firstDestructiveStep || m_cancelling) {
        return;
    }
    m_cancelling = true;
    m_cancelButton->setEnabled(false);
    m_cancelButton->setText(QStringLiteral("Stopping…"));
    m_notice->setText(QStringLiteral(
        "Cancellation requested. Waiting for a safe checkpoint; if writing has already "
        "begun, the restore will finish instead. Keep the drive connected."));
}

void RestoreDialog::finish(const QString &title, const QString &message, const QString &outcome)
{
    if (m_stage != Stage::Running) {
        return;
    }
    m_stage = Stage::Finished;
    m_timer->stop();
    updateClock();
    setWindowTitle(title);
    m_title->setText(title);
    m_message->setText(message);
    updateMessageGeometry();
    m_message->setTextInteractionFlags(Qt::TextSelectableByMouse);
    setState(m_indicator, outcome);
    setState(m_counter, outcome);
    if (outcome == QStringLiteral("complete")) {
        completeCurrentStep();
        m_progress->setValue(m_progress->maximum());
        m_counter->setText(QStringLiteral("All %1 steps completed").arg(m_rows.size()));
        m_notice->setText(
            QStringLiteral("Your USB drive is ready to use. Eject it through your system before unplugging."));
    } else {
        if (m_step > 0) {
            auto &row = m_rows[m_step - 1];
            setState(row.widget, outcome);
            setState(row.indicator, outcome);
            row.state->setText(
                outcome == QStringLiteral("error") ? QStringLiteral("Failed") : QStringLiteral("Stopped"));
        }
        m_counter->setText(
            outcome == QStringLiteral("error") ? QStringLiteral("Restore not completed")
                                               : QStringLiteral("Cancelled before erasing"));
        m_notice->setText(
            outcome == QStringLiteral("error")
                ? QStringLiteral("Read the error above before retrying. The log file contains more details.")
                : QStringLiteral(
                      "Existing volumes may need to be mounted again or have their drive letters reassigned."));
        for (int index = m_step; index < m_rows.size(); ++index) {
            m_rows[index].state->setText(QStringLiteral("Not run"));
        }
    }
    m_cancelButton->hide();
    m_doneButton->show();
    m_doneButton->setDefault(true);
    m_doneButton->setFocus();
}

void RestoreDialog::finishSuccess(const QString &message)
{
    finish(QStringLiteral("Restore complete"), message, QStringLiteral("complete"));
}

void RestoreDialog::finishFailure(const QString &message)
{
    finish(QStringLiteral("Restore failed"), message, QStringLiteral("error"));
}

void RestoreDialog::finishCancelled(const QString &message)
{
    finish(QStringLiteral("Restore cancelled"), message, QStringLiteral("cancelled"));
}

void RestoreDialog::done(int result)
{
    // Escape and the window close button must not dismiss an in-flight destructive operation.
    if (m_stage != Stage::Running) {
        if (m_stage == Stage::Confirmation) {
            m_stage = Stage::Finished;
            m_eraseButton->setEnabled(false);
            m_acknowledgement->setEnabled(false);
        }
        QDialog::done(result);
    }
}

void RestoreDialog::resizeEvent(QResizeEvent *event)
{
    QDialog::resizeEvent(event);
    updateMessageGeometry();
}

void RestoreDialog::updateMessageGeometry()
{
    if (!m_messageArea) {
        return;
    }
    const int textHeight = m_message->heightForWidth(qMax(1, width() - 64));
    m_message->setMinimumHeight(textHeight);
    m_messageArea->setFixedHeight(qBound(48, textHeight + 4, 140));
}

} // namespace usbrestore

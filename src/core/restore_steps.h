#pragma once

#include <QVector>

namespace usbrestore {

// One entry per RestoreReporter::step() call a backend makes.
enum class RestoreStep {
    VerifyDisk,
    IdentifyDevice,
    Unmount,
    OpenExclusive,
    OpenDrive,
    RefreshMetadata,
    DetachDriveLetters,
    DeletePartitions,
    LockDrive,
    ClearSignatures,
    ResetToRaw,
    WritePartitionTable,
    CreatePartition,
    RereadPartitionTable,
    WaitForPartition,
    WaitForVolume,
    RefreshNewVolume,
    AssignDriveLetter,
    Format,
    Complete,
};

// Whether the step changes what is stored on the stick; unmounting and detaching
// drive letters only change the host's view of it.
bool changesDiskContents(RestoreStep step);

// The order each backend's restore() reports its steps in.
const QVector<RestoreStep> &linuxRestorePlan();
const QVector<RestoreStep> &windowsRestorePlan();

// 1-based. The GUI says unplugging is safe only before this step.
int firstDestructiveStepOf(const QVector<RestoreStep> &plan);

// Catches a backend whose step() calls drift from its plan, which would move the
// point the GUI calls safe to unplug.
class RestoreStepCounter {
  public:
    explicit RestoreStepCounter(const QVector<RestoreStep> &plan);

    // The 1-based number of `step`, or 0 without advancing if it is not next.
    int advance(RestoreStep step);

  private:
    QVector<RestoreStep> m_plan;
    int m_done = 0;
};

} // namespace usbrestore

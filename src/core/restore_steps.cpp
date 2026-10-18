#include "core/restore_steps.h"

namespace usbrestore {

bool changesDiskContents(RestoreStep step)
{
    switch (step) {
    case RestoreStep::DeletePartitions:
    case RestoreStep::ClearSignatures:
    case RestoreStep::ResetToRaw:
    case RestoreStep::WritePartitionTable:
    case RestoreStep::CreatePartition:
    case RestoreStep::Format:
        return true;
    case RestoreStep::VerifyDisk:
    case RestoreStep::IdentifyDevice:
    case RestoreStep::Unmount:
    case RestoreStep::OpenExclusive:
    case RestoreStep::OpenDrive:
    case RestoreStep::RefreshMetadata:
    case RestoreStep::DetachDriveLetters:
    case RestoreStep::LockDrive:
    case RestoreStep::RereadPartitionTable:
    case RestoreStep::WaitForPartition:
    case RestoreStep::WaitForVolume:
    case RestoreStep::RefreshNewVolume:
    case RestoreStep::AssignDriveLetter:
    case RestoreStep::Complete:
        return false;
    }
    // An unlisted step is assumed to write, which only moves the boundary earlier.
    return true;
}

const QVector<RestoreStep> &linuxRestorePlan()
{
    static const QVector<RestoreStep> plan = {
        RestoreStep::VerifyDisk,
        RestoreStep::IdentifyDevice,
        RestoreStep::Unmount,
        RestoreStep::OpenExclusive,
        RestoreStep::ClearSignatures,
        RestoreStep::WritePartitionTable,
        RestoreStep::RereadPartitionTable,
        RestoreStep::WaitForPartition,
        RestoreStep::Format,
        RestoreStep::Complete,
    };
    return plan;
}

const QVector<RestoreStep> &windowsRestorePlan()
{
    static const QVector<RestoreStep> plan = {
        RestoreStep::VerifyDisk,
        RestoreStep::OpenDrive,
        RestoreStep::RefreshMetadata,
        RestoreStep::DetachDriveLetters,
        RestoreStep::DeletePartitions,
        RestoreStep::LockDrive,
        RestoreStep::ClearSignatures,
        RestoreStep::ResetToRaw,
        RestoreStep::CreatePartition,
        RestoreStep::WaitForVolume,
        RestoreStep::RefreshNewVolume,
        RestoreStep::AssignDriveLetter,
        RestoreStep::Format,
        RestoreStep::Complete,
    };
    return plan;
}

int firstDestructiveStepOf(const QVector<RestoreStep> &plan)
{
    for (int index = 0; index < plan.size(); ++index) {
        if (changesDiskContents(plan.at(index))) {
            return index + 1;
        }
    }
    return static_cast<int>(plan.size()) + 1;
}

RestoreStepCounter::RestoreStepCounter(const QVector<RestoreStep> &plan) : m_plan(plan) {}

int RestoreStepCounter::advance(RestoreStep step)
{
    if (m_done >= m_plan.size() || m_plan.at(m_done) != step) {
        return 0;
    }
    return ++m_done;
}

} // namespace usbrestore

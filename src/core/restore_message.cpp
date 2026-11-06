#include "core/restore_message.h"

#include <QCoreApplication>

namespace usbrestore {

RestoreMessage restoreText(const char *source)
{
    RestoreMessage message{QString::fromUtf8(source), QCoreApplication::translate("usbrestore::Backend", source)};
    message.m_source = source;
    return message;
}

QString RestoreMessage::translated() const
{
    if (m_translate) {
        return (*m_translate)();
    }
    if (m_source) {
        return QCoreApplication::translate("usbrestore::Backend", m_source);
    }
    return display;
}

RestoreMessage RestoreMessage::trimmed() const
{
    RestoreMessage result{english.trimmed(), display.trimmed()};
    if (isTranslatable()) {
        result.m_translate =
            std::make_shared<const Translation>([message = *this]() { return message.translated().trimmed(); });
    }
    return result;
}

RestoreMessage formatByteSizeMessage(std::uint64_t bytes)
{
    return RestoreMessage{formatByteSize(bytes), formatByteSize(bytes, TextMode::Display)};
}

RestoreMessage restoreStepMessage(RestoreStep step, PartitionStyle style, FileSystemType fileSystem)
{
    switch (step) {
    case RestoreStep::VerifyDisk:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Verifying the selected USB disk"));
    case RestoreStep::IdentifyDevice:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Identifying the block device"));
    case RestoreStep::Unmount:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Unmounting existing filesystems"));
    case RestoreStep::OpenExclusive:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Opening the device exclusively"));
    case RestoreStep::OpenDrive:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Opening and identifying the physical drive"));
    case RestoreStep::RefreshMetadata:
    case RestoreStep::RefreshNewVolume:
        // Windows refreshes the same metadata before and after the new volume appears.
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Refreshing Windows disk metadata"));
    case RestoreStep::DetachDriveLetters:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Detaching existing volumes and drive letters"));
    case RestoreStep::DeletePartitions:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Deleting existing partition records"));
    case RestoreStep::LockDrive:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Locking the physical drive"));
    case RestoreStep::ClearSignatures:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Clearing old MBR, GPT and ISO signatures"));
    case RestoreStep::ResetToRaw:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Resetting the disk to RAW"));
    case RestoreStep::WritePartitionTable:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Writing the %1 partition table"))
            .arg(partitionStyleLabel(style));
    case RestoreStep::CreatePartition:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Creating one %1 data partition"))
            .arg(partitionStyleLabel(style));
    case RestoreStep::RereadPartitionTable:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Re-reading the partition table"));
    case RestoreStep::WaitForPartition:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Waiting for the new partition to appear"));
    case RestoreStep::WaitForVolume:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Waiting for Windows to discover the new volume"));
    case RestoreStep::AssignDriveLetter:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Assigning a drive letter"));
    case RestoreStep::Format:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Formatting as %1"))
            .arg(fileSystemTypeName(fileSystem));
    case RestoreStep::Complete:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Restore complete"));
    }
    return {};
}

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
        RestoreStep::Format,
        RestoreStep::RereadPartitionTable,
        RestoreStep::WaitForPartition,
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

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
    case RestoreStep::RefreshNewVolume:
        return restoreText(QT_TRANSLATE_NOOP("usbrestore::Backend", "Refreshing Windows disk metadata"));
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

} // namespace usbrestore

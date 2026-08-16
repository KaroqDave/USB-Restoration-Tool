#include "gui/app_settings.h"

#include <QSettings>

namespace usbrestore {

namespace {

constexpr auto SettingsOrganization = "KaroqDave";
constexpr auto SettingsApplication = "USB Restoration Tool";

// Nothing about a disk is remembered between runs, deliberately: a restore
// target has to be chosen against what is plugged in right now, not restored
// from a file written the last time the app ran.

} // namespace

AppSettings loadAppSettings()
{
    QSettings settings(QString::fromLatin1(SettingsOrganization), QString::fromLatin1(SettingsApplication));
    AppSettings result;
    if (settings.contains(QStringLiteral("theme"))) {
        result.theme = themeFromSettings(settings.value(QStringLiteral("theme")).toInt());
    }
    result.geometry = settings.value(QStringLiteral("geometry")).toByteArray();

    const int style = settings.value(QStringLiteral("partitionStyle"),
                                     static_cast<int>(PartitionStyle::Gpt))
                          .toInt();
    result.partitionStyle = style == static_cast<int>(PartitionStyle::Mbr) ? PartitionStyle::Mbr : PartitionStyle::Gpt;

    const int fileSystem = settings.value(QStringLiteral("fileSystem"), static_cast<int>(FileSystemType::ExFat)).toInt();
    FileSystemType parsed = FileSystemType::ExFat;
    switch (static_cast<FileSystemType>(fileSystem)) {
    case FileSystemType::ExFat:
    case FileSystemType::Fat32:
    case FileSystemType::Ntfs:
    case FileSystemType::Ext4:
        parsed = static_cast<FileSystemType>(fileSystem);
        break;
    }
    result.fileSystem = parsed;
    return result;
}

void saveAppSettings(const AppSettings &settings)
{
    QSettings store(QString::fromLatin1(SettingsOrganization), QString::fromLatin1(SettingsApplication));
    store.setValue(QStringLiteral("theme"), themeToSettings(settings.theme));
    store.setValue(QStringLiteral("geometry"), settings.geometry);
    store.setValue(QStringLiteral("partitionStyle"), static_cast<int>(settings.partitionStyle));
    store.setValue(QStringLiteral("fileSystem"), static_cast<int>(settings.fileSystem));
}

} // namespace usbrestore

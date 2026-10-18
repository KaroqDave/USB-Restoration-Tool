#pragma once

#include "core/disk.h"
#include "gui/theme.h"

#include <QByteArray>

namespace usbrestore {

struct AppSettings {
    Theme theme = Theme::System;
    QByteArray geometry;
    // A preference about the person's devices (e.g. MBR for a car stereo), not any one disk.
    PartitionStyle partitionStyle = PartitionStyle::Gpt;
    FileSystemType fileSystem = FileSystemType::ExFat;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &settings);

} // namespace usbrestore

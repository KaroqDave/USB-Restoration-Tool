#pragma once

#include "core/disk.h"
#include "gui/theme.h"

#include <QByteArray>

namespace usbrestore {

struct AppSettings {
    Theme theme = Theme::System;
    QByteArray geometry;
    // Remembered because someone restoring sticks for an old car stereo wants
    // MBR every time, not once. It is a preference about the person's devices,
    // not about any particular disk.
    PartitionStyle partitionStyle = PartitionStyle::Gpt;
    FileSystemType fileSystem = FileSystemType::ExFat;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &settings);

} // namespace usbrestore

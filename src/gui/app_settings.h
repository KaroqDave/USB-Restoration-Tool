#pragma once

#include "gui/theme.h"

#include <QByteArray>

namespace usbrestore {

struct AppSettings {
    Theme theme = Theme::System;
    QByteArray geometry;
};

AppSettings loadAppSettings();
void saveAppSettings(const AppSettings &settings);

} // namespace usbrestore

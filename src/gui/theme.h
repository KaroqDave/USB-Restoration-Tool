#pragma once

#include <QColor>
#include <QString>

class QApplication;

namespace usbrestore {

enum class Theme {
    System,
    Light,
    Dark,
};

enum class ColorScheme {
    Light,
    Dark,
};

// Each maps to one accent colour so it reads the same in both colour schemes.
enum class StatusKind {
    Info,
    Ready,
    Warning,
    Blocked,
    Running,
    Success,
    Error,
};

struct Palette {
    QColor background;
    QColor surface;
    QColor surfaceAlt;
    QColor border;
    QColor text;
    QColor mutedText;
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentText;
    QColor accentSoft;
    QColor warningSurface;
    QColor danger;
    QColor dangerHover;
    QColor dangerPressed;
    QColor statusSuccess;
    QColor statusWarning;
    QColor statusError;
    QColor statusInfo;
    QColor statusMuted;
    bool isDark = false;
};

Palette lightPalette();
Palette darkPalette();

ColorScheme resolveColorScheme(Theme theme);
const Palette &paletteFor(ColorScheme scheme);

QString buildStyleSheet(const Palette &palette);

Theme themeFromSettings(int value);
int themeToSettings(Theme theme);
QString themeButtonText(Theme theme);

void applyTheme(QApplication &app, Theme theme);

QString statusBadgePrefix(StatusKind status);
QColor statusBadgeBackground(StatusKind status, const Palette &palette);
QColor statusBadgeText(StatusKind status, const Palette &palette);

} // namespace usbrestore

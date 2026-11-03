#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>

namespace usbrestore {

// Shared backend messages default to English so installing a UI catalog cannot change logs.
enum class TextMode {
    English,
    Display,
};

QString coreText(const char *source, TextMode mode);
// The catalog chooses this, so formatting follows the UI without changing the process locale.
QLocale displayLocale();
QString formatDecimal(double value, int precision, TextMode mode = TextMode::English);

enum class Language {
    System,
    English,
    German,
};

Language languageFromSettings(const QString &value);
QString languageToSettings(Language language);
Language resolveLanguage(Language preference, const QStringList &uiLanguages = QLocale::system().uiLanguages());
QString languageButtonText(Language language);

} // namespace usbrestore

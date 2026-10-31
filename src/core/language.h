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
QString formatDecimal(double value, int precision, TextMode mode = TextMode::English);

enum class Language {
    System,
    English,
    German,
};

Language languageFromSettings(const QString &value);
QString languageToSettings(Language language);
Language resolveLanguage(Language preference, const QStringList &uiLanguages = QLocale::system().uiLanguages());

} // namespace usbrestore

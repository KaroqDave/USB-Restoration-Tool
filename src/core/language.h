#pragma once

#include <QLocale>
#include <QString>
#include <QStringList>

namespace usbrestore {

enum class Language {
    System,
    English,
    German,
};

Language languageFromSettings(const QString &value);
QString languageToSettings(Language language);
Language resolveLanguage(Language preference, const QStringList &uiLanguages = QLocale::system().uiLanguages());

} // namespace usbrestore

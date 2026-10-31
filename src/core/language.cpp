#include "core/language.h"

namespace usbrestore {

Language languageFromSettings(const QString &value)
{
    if (value == QStringLiteral("en")) {
        return Language::English;
    }
    if (value == QStringLiteral("de")) {
        return Language::German;
    }
    return Language::System;
}

QString languageToSettings(Language language)
{
    switch (language) {
    case Language::English:
        return QStringLiteral("en");
    case Language::German:
        return QStringLiteral("de");
    case Language::System:
        break;
    }
    return QStringLiteral("system");
}

Language resolveLanguage(Language preference, const QStringList &uiLanguages)
{
    if (preference == Language::English || preference == Language::German) {
        return preference;
    }
    for (const QString &name : uiLanguages) {
        switch (QLocale(name).language()) {
        case QLocale::German:
            return Language::German;
        case QLocale::English:
            return Language::English;
        default:
            break;
        }
    }
    return Language::English;
}

} // namespace usbrestore

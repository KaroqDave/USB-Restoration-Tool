#include "core/language.h"

#include <QCoreApplication>

namespace usbrestore {

QString coreText(const char *source, TextMode mode)
{
    return mode == TextMode::Display ? QCoreApplication::translate("usbrestore::Core", source)
                                     : QString::fromUtf8(source);
}

QString formatDecimal(double value, int precision, TextMode mode)
{
    // The catalog chooses the display locale without changing process-wide formatting or logs.
    const char *localeName = QT_TRANSLATE_NOOP("usbrestore::Core", "en_US");
    QLocale locale = mode == TextMode::Display ? QLocale(coreText(localeName, mode)) : QLocale::c();
    locale.setNumberOptions(QLocale::OmitGroupSeparator);
    return locale.toString(value, 'f', precision);
}

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

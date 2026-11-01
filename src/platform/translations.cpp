#include "platform/translations.h"

#include <QCoreApplication>

namespace usbrestore {

Translations::~Translations()
{
    unload();
}

void Translations::unload()
{
    QCoreApplication::removeTranslator(&m_application);
    QCoreApplication::removeTranslator(&m_qt);
    m_language = Language::English;
}

bool Translations::load(Language language)
{
    unload();
    const Language resolved = resolveLanguage(language);
    if (resolved == Language::English) {
        return true;
    }
    if (!m_application.load(QStringLiteral(":/i18n/usbrestore_de.qm")) ||
        !m_qt.load(QStringLiteral(":/i18n/qtbase_de.qm"))) {
        return false;
    }
    if (!QCoreApplication::installTranslator(&m_qt) || !QCoreApplication::installTranslator(&m_application)) {
        unload();
        return false;
    }
    m_language = resolved;
    return true;
}

bool activateInterfaceLanguage(Translations &translations, Language preference)
{
    if (translations.load(preference)) {
        return true;
    }
    translations.load(Language::English);
    return false;
}

QString elevatedPermissionTitle()
{
    return QCoreApplication::translate("usbrestore::Startup", "Elevated permission required");
}

CatalogProbe catalogProbe(const QStringList &arguments)
{
    CatalogProbe result;
    if (arguments.isEmpty() || arguments.first() != QLatin1String(ShowTranslationFlag)) {
        return result;
    }
    result.kind = CatalogProbeKind::Usage;
    if (arguments.size() != 2) {
        return result;
    }

    const QString token = arguments.at(1);
    Language language = Language::System;
    if (token == QLatin1String("en")) {
        language = Language::English;
    } else if (token == QLatin1String("de")) {
        language = Language::German;
    } else {
        return result;
    }

    Translations translations;
    if (!activateInterfaceLanguage(translations, language)) {
        result.kind = CatalogProbeKind::MissingCatalog;
        return result;
    }
    result.kind = CatalogProbeKind::Ready;
    result.line = elevatedPermissionTitle();
    return result;
}

} // namespace usbrestore

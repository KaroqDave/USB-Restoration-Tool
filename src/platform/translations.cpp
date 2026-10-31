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

} // namespace usbrestore

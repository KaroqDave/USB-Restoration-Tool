#pragma once

#include "core/language.h"

#include <QTranslator>

namespace usbrestore {

// Own for the application's lifetime, and load before constructing widgets.
// Only embedded catalogs are read, including when the process is elevated.
class Translations {
  public:
    ~Translations();

    bool load(Language language);
    Language language() const { return m_language; }

  private:
    void unload();

    QTranslator m_application;
    QTranslator m_qt;
    Language m_language = Language::English;
};

// A missing catalog is not fatal: English stays installed and the caller still opens.
bool activateInterfaceLanguage(Translations &translations, Language preference);

QString elevatedPermissionTitle();

// Packaging probe. It is not a restore argument and never opens a disk.
inline constexpr char ShowTranslationFlag[] = "--show-translation";

enum class CatalogProbeKind {
    NotRequested,
    Ready,
    Usage,
    MissingCatalog,
};

struct CatalogProbe {
    CatalogProbeKind kind = CatalogProbeKind::NotRequested;
    QString line;
};

// Accepts only `--show-translation en` or `--show-translation de`. German fails
// when its catalog does not load, instead of printing English.
CatalogProbe catalogProbe(const QStringList &arguments);

} // namespace usbrestore

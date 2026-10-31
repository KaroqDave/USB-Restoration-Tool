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

} // namespace usbrestore

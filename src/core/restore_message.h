#pragma once

#include "core/disk.h"
#include "core/restore_steps.h"

#include <functional>
#include <memory>
#include <utility>

namespace usbrestore {

// Keep application wording separate from diagnostics supplied by the OS or a formatter.
struct RestoreMessage {
    QString english;
    QString display;

    RestoreMessage() = default;
    RestoreMessage(const QString &text) : english(text), display(text) {}
    RestoreMessage(QString englishText, QString displayText)
        : english(std::move(englishText)), display(std::move(displayText))
    {
    }

    template <typename... Args> RestoreMessage arg(const Args &...args) const
    {
        RestoreMessage result{
            english.arg(argument(args, TextMode::English)...), display.arg(argument(args, TextMode::Display)...)};
        if (isTranslatable() || (isTranslatable(args) || ...)) {
            result.m_translate = std::make_shared<const Translation>(
                [format = *this, args...]() { return format.translated().arg(translatedArgument(args)...); });
        }
        return result;
    }

    RestoreMessage trimmed() const;

    // Display text in the current language, for text that stays on screen across a language change.
    // Only restoreText() and arg() are tracked; assigning english or display directly is not.
    QString translated() const;

  private:
    using Translation = std::function<QString()>;

    friend RestoreMessage restoreText(const char *source);

    bool isTranslatable() const { return m_source || m_translate; }
    static bool isTranslatable(const RestoreMessage &message) { return message.isTranslatable(); }
    template <typename T> static bool isTranslatable(const T &) { return false; }

    static const QString &argument(const RestoreMessage &message, TextMode mode)
    {
        return mode == TextMode::Display ? message.display : message.english;
    }
    static const QString &argument(const QString &value, TextMode) { return value; }
    template <typename T> static QString argument(const T &value, TextMode) { return QString::number(value); }

    static QString translatedArgument(const RestoreMessage &message) { return message.translated(); }
    static const QString &translatedArgument(const QString &value) { return value; }
    template <typename T> static QString translatedArgument(const T &value) { return QString::number(value); }

    const char *m_source = nullptr;
    std::shared_ptr<const Translation> m_translate;
};

RestoreMessage restoreText(const char *source);
RestoreMessage restoreStepMessage(RestoreStep step, PartitionStyle style, FileSystemType fileSystem);

// The same size in both renderings, which every size-mismatch refusal needs.
RestoreMessage formatByteSizeMessage(std::uint64_t bytes);

inline void setRestoreError(QString *error, QString *displayError, const RestoreMessage &message)
{
    if (error) {
        *error = message.english;
    }
    if (displayError) {
        *displayError = message.display;
    }
}

// Only pure core checks belong here: their English result alone decides whether to proceed.
template <typename Check> bool checkRestoreError(QString *error, QString *displayError, Check check)
{
    RestoreMessage message;
    if (check(&message.english, TextMode::English)) {
        return true;
    }
    if (displayError) {
        check(&message.display, TextMode::Display);
    }
    setRestoreError(error, displayError, message);
    return false;
}

} // namespace usbrestore

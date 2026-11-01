#pragma once

#include "core/disk.h"
#include "core/restore_steps.h"

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
        return {english.arg(argument(args, TextMode::English)...), display.arg(argument(args, TextMode::Display)...)};
    }

  private:
    static const QString &argument(const RestoreMessage &message, TextMode mode)
    {
        return mode == TextMode::Display ? message.display : message.english;
    }
    template <typename T> static const T &argument(const T &value, TextMode) { return value; }
};

RestoreMessage restoreText(const char *source);
RestoreMessage restoreStepMessage(RestoreStep step, PartitionStyle style, FileSystemType fileSystem);

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

#pragma once

#include "core/restore_message.h"

#include <QString>
#include <cstdint>

namespace usbrestore {

// Exposed so tests can check that loop-mapping errors do not blame the USB drive.
RestoreMessage loopErrorMessage(const RestoreMessage &operation, int errorNumber, const RestoreMessage &advice);
QString loopErrorText(const QString &operation, int errorNumber, const QString &advice);

// The kernel retains the backing file, so a formatter never reopens the USB pathname.
class LoopDevice {
  public:
    ~LoopDevice();
    LoopDevice() = default;
    LoopDevice(const LoopDevice &) = delete;
    LoopDevice &operator=(const LoopDevice &) = delete;

    bool configure(
        int backingDescriptor,
        std::uint64_t offset,
        std::uint64_t length,
        quint32 sectorSize,
        QString *error,
        QString *displayError = nullptr);
    bool prepareForFormatting(QString *error, QString *displayError = nullptr);
    bool detach(QString *error, QString *displayError = nullptr);
    QString path() const { return m_path; }

  private:
    void releaseClaim();
    int m_fd = -1;
    int m_claimFd = -1;
    QString m_path;
};

} // namespace usbrestore

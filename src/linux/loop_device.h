#pragma once

#include <QString>
#include <cstdint>

namespace usbrestore {

// Exposed so tests can check that loop-mapping errors do not blame the USB drive.
QString loopErrorText(const QString &operation, int errorNumber, const QString &advice);

// The kernel retains the backing file, so a formatter never reopens the USB pathname.
class LoopDevice {
  public:
    ~LoopDevice();
    LoopDevice() = default;
    LoopDevice(const LoopDevice &) = delete;
    LoopDevice &operator=(const LoopDevice &) = delete;

    bool
    configure(int backingDescriptor, std::uint64_t offset, std::uint64_t length, quint32 sectorSize, QString *error);
    bool prepareForFormatting(QString *error);
    bool detach(QString *error);
    QString path() const { return m_path; }

  private:
    void releaseClaim();
    int m_fd = -1;
    int m_claimFd = -1;
    QString m_path;
};

} // namespace usbrestore

#include "gui/app_settings.h"
#include "gui/main_window.h"
#include "gui/theme.h"
#include "platform/disk_service.h"
#include "platform/startup.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>
#include <QtGlobal>

#ifdef Q_OS_WIN
#include "win/wmi.h"
#include <objbase.h>
#endif

#ifdef Q_OS_LINUX
#include <unistd.h>
#endif

#ifndef USBRESTORE_APP_VERSION
#define USBRESTORE_APP_VERSION "0.0.0-dev"
#endif

namespace {

#ifdef Q_OS_WIN
// Enters the STA here, not in whichever component gets there first, so that
// CoInitializeSecurity runs before any interface is marshalled.
class ComScope {
  public:
    ComScope()
    {
        const HRESULT result = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        m_initialized = SUCCEEDED(result);
    }

    ~ComScope()
    {
        if (m_initialized) {
            CoUninitialize();
        }
    }

    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;

  private:
    bool m_initialized = false;
};
#endif

} // namespace

int main(int argc, char *argv[])
{
    // Before the first DLL load and before Qt starts, on both platforms.
    usbrestore::hardenProcessStartup();

#ifdef Q_OS_LINUX
    // sudo strips the session D-Bus address, so Qt's GNOME theme warns on every root
    // start; silence it only when the bus is really absent and no rules were given.
    if (::geteuid() == 0 && !qEnvironmentVariableIsSet("DBUS_SESSION_BUS_ADDRESS")
        && !qEnvironmentVariableIsSet("QT_LOGGING_RULES")) {
        qputenv("QT_LOGGING_RULES", QByteArrayLiteral("qt.qpa.theme.*=false"));
    }

    // GNOME Wayland refuses server-side decorations and Qt's fallback has no window
    // buttons (Ubuntu 26.04), so prefer XWayland there; wayland stays the fallback.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM") && qEnvironmentVariableIsSet("WAYLAND_DISPLAY")
        && qEnvironmentVariable("XDG_CURRENT_DESKTOP").contains(QStringLiteral("GNOME"), Qt::CaseInsensitive)) {
        qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("xcb;wayland"));
    }
#endif

#ifdef Q_OS_WIN
    const ComScope com;
    usbrestore::initializeComSecurity();
#endif

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KaroqDave"));
    QApplication::setApplicationName(QStringLiteral("USB Restoration Tool"));
    QApplication::setApplicationVersion(QStringLiteral(USBRESTORE_APP_VERSION));
    QApplication::setDesktopFileName(QStringLiteral("usb-restoration-tool"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    const std::unique_ptr<usbrestore::DiskService> service = usbrestore::DiskService::create();

    // Windows elevates via the manifest; on Linux false means no helper and not root.
    // Either would otherwise fail partway through rather than up front.
    if (!service->isPrivileged()) {
        QMessageBox::critical(nullptr, QStringLiteral("Elevated permission required"), service->privilegeHint());
        return 1;
    }

    const usbrestore::AppSettings settings = usbrestore::loadAppSettings();
    usbrestore::applyTheme(app, settings.theme);

    usbrestore::MainWindow window(*service, settings.theme);
    window.show();

    return app.exec();
}

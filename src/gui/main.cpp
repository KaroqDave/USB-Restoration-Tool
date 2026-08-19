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

#ifndef USBRESTORE_APP_VERSION
#define USBRESTORE_APP_VERSION "0.0.0-dev"
#endif

namespace {

#ifdef Q_OS_WIN
// Holds the process COM apartment open for the lifetime of main(). The GUI
// thread enters an STA here rather than leaving it to whichever component gets
// there first, so that CoInitializeSecurity below runs before any interface is
// marshalled.
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
    // Before anything else runs: on Windows this narrows the DLL search order
    // before the first DLL loads, on Linux it disables core dumps and tightens
    // the file mode. Both have to happen before Qt starts.
    usbrestore::hardenProcessStartup();

#ifdef Q_OS_LINUX
    // GNOME's Wayland compositor refuses server-side window decorations, and
    // the fallback Qt draws itself is a bare white strip with no close,
    // minimize or maximize buttons — confirmed on Ubuntu 26.04, 2026-08-20,
    // where the same build under XWayland gets ordinary GNOME decorations.
    // So on a GNOME Wayland session, prefer xcb, keeping wayland as the
    // fallback for a session with no XWayland. Compositors that provide
    // decorations (KDE and the wlroots family speak xdg-decoration) are left
    // alone, and so is anyone who chose a platform themselves.
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

    // Windows asks for elevation through the manifest, so reaching this without
    // it means something bypassed that. On Linux this process no longer needs
    // privilege at all — it asks for it when a restore starts — so a false here
    // means neither route is open: no helper installed and not root either.
    // Both cases would otherwise fail partway through rather than up front.
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

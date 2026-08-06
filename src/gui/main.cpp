#include "gui/app_settings.h"
#include "gui/main_window.h"
#include "gui/theme.h"
#include "win/admin.h"
#include "win/wmi.h"

#include <QApplication>
#include <QIcon>
#include <QMessageBox>

#include <objbase.h>

#ifndef USBRESTORE_APP_VERSION
#define USBRESTORE_APP_VERSION "0.0.0-dev"
#endif

namespace {

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

} // namespace

int main(int argc, char *argv[])
{
    // Before anything else loads a DLL: this process runs as Administrator, and
    // the default search order would otherwise include directories an
    // unprivileged user can write to.
    usbrestore::enableSecureDllSearch();

    const ComScope com;
    usbrestore::initializeComSecurity();

    QApplication app(argc, argv);
    QApplication::setOrganizationName(QStringLiteral("KaroqDave"));
    QApplication::setApplicationName(QStringLiteral("USB Restoration Tool"));
    QApplication::setApplicationVersion(QStringLiteral(USBRESTORE_APP_VERSION));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/icons/app.ico")));

    // The manifest asks Windows for elevation, so reaching this without it
    // means something bypassed the manifest. Raw disk access would fail
    // partway through rather than up front, so stop here instead.
    if (!usbrestore::isProcessElevated()) {
        QMessageBox::critical(nullptr,
                              QStringLiteral("Administrator permission required"),
                              QStringLiteral("USB Restoration Tool needs Administrator permission to restore USB "
                                             "disks.\n\nClose this window and start the app again, approving the "
                                             "Windows permission prompt."));
        return 1;
    }

    const usbrestore::AppSettings settings = usbrestore::loadAppSettings();
    usbrestore::applyTheme(app, settings.theme);

    usbrestore::MainWindow window(settings.theme);
    window.show();

    return app.exec();
}

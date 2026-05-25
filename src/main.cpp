#include "gui/MainWindow.h"
#include "win/Admin.h"

#include <QApplication>
#include <QMessageBox>

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName(QStringLiteral("USB Restoration Tool"));
    app.setApplicationVersion(QStringLiteral("0.1.0"));
    app.setOrganizationName(QStringLiteral("USB Restoration Tool"));

    if (!usbrestore::isProcessElevated()) {
        if (usbrestore::relaunchElevated()) {
            return 0;
        }
        QMessageBox::critical(nullptr,
                              QStringLiteral("Administrator permission required"),
                              QStringLiteral("USB Restoration Tool needs Administrator permission to restore USB disks."));
        return 1;
    }

    usbrestore::MainWindow window;
    window.show();
    return app.exec();
}

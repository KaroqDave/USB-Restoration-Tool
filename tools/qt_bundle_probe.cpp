#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QPixmap>
#include <QTextStream>

#include <dlfcn.h>

int main(int argc, char *argv[])
{
    if (argc != 3) {
        QTextStream(stderr) << "Usage: usb-qt-bundle-probe <bundled-plugin-directory> <svg-icon>" << Qt::endl;
        return 1;
    }

    const QString pluginDirectory = QString::fromLocal8Bit(argv[1]);
    const QString iconPath = QString::fromLocal8Bit(argv[2]);
    QCoreApplication::setLibraryPaths({pluginDirectory});
    qputenv("QT_QPA_PLATFORM", QByteArrayLiteral("offscreen"));
    qputenv("QT_QPA_PLATFORMTHEME", QByteArray());
    qputenv("QT_STYLE_OVERRIDE", QByteArrayLiteral("Fusion"));

    QApplication app(argc, argv);
    const QPixmap icon = QIcon(iconPath).pixmap(32, 32);
    if (icon.isNull()) {
        QTextStream(stderr) << "The bundled Qt SVG plugin could not render " << iconPath << Qt::endl;
        return 1;
    }

    // instance() is inline, and the address of an imported function can be a PLT
    // stub in this executable. qVersion lives in the QtCore that was actually mapped.
    void *qtCore = dlopen("libQt6Core.so.6", RTLD_LAZY | RTLD_NOLOAD);
    void *qtCoreSymbol = qtCore == nullptr ? nullptr : dlsym(qtCore, "qVersion");
    Dl_info loadedQtCore{};
    if (qtCoreSymbol == nullptr || dladdr(qtCoreSymbol, &loadedQtCore) == 0 || loadedQtCore.dli_fname == nullptr) {
        QTextStream(stderr) << "Could not locate the loaded QtCore library." << Qt::endl;
        return 1;
    }

    QTextStream(stdout) << "Loaded QtCore: " << QString::fromLocal8Bit(loadedQtCore.dli_fname) << Qt::endl;
    QTextStream(stdout) << "Bundled Qt platform and SVG loading passed." << Qt::endl;
    return 0;
}

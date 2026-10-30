#include <QApplication>
#include <QCoreApplication>
#include <QIcon>
#include <QPixmap>
#include <QTextStream>

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

    QTextStream(stdout) << "Bundled Qt platform and SVG loading passed." << Qt::endl;
    return 0;
}

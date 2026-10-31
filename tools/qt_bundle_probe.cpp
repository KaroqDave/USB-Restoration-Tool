#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QIcon>
#include <QPixmap>
#include <QStringList>
#include <QTextStream>

#include <link.h>

int main(int argc, char *argv[])
{
    if (argc != 3 && argc != 4) {
        QTextStream(stderr) << "Usage: usb-qt-bundle-probe <plugin-directory> <svg-icon> [bundled-library-directory]"
                            << Qt::endl;
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

    // Plugins can load more Qt libraries, so inspect the mappings after rendering.
    QStringList libraries;
    dl_iterate_phdr(
        [](dl_phdr_info *info, size_t, void *data) {
            const QString path = QString::fromLocal8Bit(info->dlpi_name);
            if (QFileInfo(path).fileName().startsWith(QStringLiteral("libQt6"))) {
                static_cast<QStringList *>(data)->append(path);
            }
            return 0;
        },
        &libraries);
    if (libraries.isEmpty()) {
        QTextStream(stderr) << "Could not locate the loaded Qt libraries." << Qt::endl;
        return 1;
    }

    QString libraryDirectory;
    if (argc == 4) {
        libraryDirectory = QFileInfo(QString::fromLocal8Bit(argv[3])).canonicalFilePath();
        if (libraryDirectory.isEmpty() || !QFileInfo(libraryDirectory).isDir()) {
            QTextStream(stderr) << "The bundled Qt library directory does not exist: " << argv[3] << Qt::endl;
            return 1;
        }
    }
    for (const QString &library : libraries) {
        const QString path = QFileInfo(library).canonicalFilePath();
        QTextStream(stdout) << "Loaded Qt library: " << path << Qt::endl;
        if (path.isEmpty() || (!libraryDirectory.isEmpty() && !path.startsWith(libraryDirectory + QDir::separator()))) {
            QTextStream(stderr) << "Qt library loaded outside the bundle: " << library << " (expected inside "
                                << libraryDirectory << ")." << Qt::endl;
            return 1;
        }
    }

    QTextStream(stdout) << "Qt platform and SVG loading passed." << Qt::endl;
    return 0;
}

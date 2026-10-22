#include <QBuffer>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTextStream>

namespace usbrestore {

QImage renderSvg(const QString &path, const QSize &size)
{
    QImageReader reader(path);
    reader.setScaledSize(size * 4);
    const QImage image = reader.read();
    if (image.isNull()) {
        QTextStream(stderr) << "Cannot render " << path << ": " << reader.errorString() << Qt::endl;
        return {};
    }
    return image.scaled(size, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
}

bool exportBranding(const QDir &root)
{
    const QString source = root.filePath(QStringLiteral("resources/icons/app.svg"));
    const QImage icon = renderSvg(source, QSize(256, 256));
    if (icon.isNull() || !icon.save(root.filePath(QStringLiteral("resources/icons/app.png")))) {
        return false;
    }

    const QList<int> sizes = {16, 20, 24, 32, 40, 48, 64, 128, 256};
    QList<QByteArray> frames;
    for (int size : sizes) {
        const QImage frame = renderSvg(source, QSize(size, size));
        QByteArray png;
        QBuffer buffer(&png);
        if (frame.isNull() || !buffer.open(QIODevice::WriteOnly) || !frame.save(&buffer, "PNG")) {
            return false;
        }
        frames.append(png);
    }

    QFile ico(root.filePath(QStringLiteral("resources/icons/app.ico")));
    if (!ico.open(QIODevice::WriteOnly)) {
        return false;
    }
    QDataStream stream(&ico);
    stream.setByteOrder(QDataStream::LittleEndian);
    stream << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * quint32(sizes.size());
    for (qsizetype i = 0; i < sizes.size(); ++i) {
        const quint8 edge = sizes[i] == 256 ? 0 : quint8(sizes[i]);
        stream << edge << edge << quint8(0) << quint8(0) << quint16(1) << quint16(32) << quint32(frames[i].size())
               << offset;
        offset += quint32(frames[i].size());
    }
    for (const QByteArray &frame : frames) {
        if (stream.writeRawData(frame.constData(), frame.size()) != frame.size()) {
            return false;
        }
    }
    if (stream.status() != QDataStream::Ok || !ico.flush()) {
        return false;
    }

    const QImage banner =
        renderSvg(root.filePath(QStringLiteral("resources/branding/readme-banner.svg")), QSize(1200, 320));
    return !banner.isNull() && banner.save(root.filePath(QStringLiteral("resources/branding/readme-banner.png")));
}

} // namespace usbrestore

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);
    const QStringList args = app.arguments();
    if (args.size() != 2) {
        QTextStream(stderr) << "Usage: usb-branding-export <project-root> [-platform offscreen]" << Qt::endl;
        return 1;
    }
    QDir root(args[1]);
    if (!root.makeAbsolute() || !usbrestore::exportBranding(root)) {
        QTextStream(stderr) << "Branding export failed. Check the source SVGs, SVG image plugin and output paths."
                            << Qt::endl;
        return 1;
    }
    QTextStream(stdout) << "Exported app.png, app.ico (9 sizes) and readme-banner.png." << Qt::endl;
    return 0;
}

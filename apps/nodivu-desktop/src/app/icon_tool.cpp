#include "ui/theme/brand_icon.hpp"
#include <QBuffer>
#include <QCoreApplication>
#include <QDataStream>
#include <QFile>

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    if (app.arguments().size() != 2)
        return 2;
    const QList<int> sizes{16, 24, 32, 48, 64, 128, 256};
    QList<QByteArray> images;
    for (const auto size : sizes) {
        QByteArray bytes;
        QBuffer buffer(&bytes);
        if (!buffer.open(QIODevice::WriteOnly) || !nodivu::brandImage(size).save(&buffer, "PNG"))
            return 1;
        images.append(bytes);
    }
    QFile file(app.arguments()[1]);
    if (!file.open(QIODevice::WriteOnly))
        return 1;
    QDataStream output(&file);
    output.setByteOrder(QDataStream::LittleEndian);
    output << quint16(0) << quint16(1) << quint16(sizes.size());
    quint32 offset = 6 + 16 * static_cast<quint32>(sizes.size());
    for (qsizetype index = 0; index < sizes.size(); ++index) {
        const auto dimension = static_cast<quint8>(sizes[index] == 256 ? 0 : sizes[index]);
        output << dimension << dimension << quint8(0) << quint8(0) << quint16(1) << quint16(32)
               << quint32(images[index].size()) << offset;
        offset += static_cast<quint32>(images[index].size());
    }
    for (const auto& image : images)
        output.writeRawData(image.constData(), image.size());
    return output.status() == QDataStream::Ok ? 0 : 1;
}

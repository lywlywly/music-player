#ifndef ARTWORKUTILS_H
#define ARTWORKUTILS_H

#include <QBuffer>
#include <QByteArray>
#include <QImage>

namespace ArtworkUtils {

inline QByteArray normalizeArtworkToPng(const QByteArray &imageData,
                                        int maxSide) {
  if (imageData.isEmpty() || maxSide <= 0) {
    return {};
  }

  QImage image;
  if (!image.loadFromData(imageData)) {
    return {};
  }

  if (image.width() > maxSide || image.height() > maxSide) {
    image = image.scaled(maxSide, maxSide, Qt::KeepAspectRatio,
                         Qt::SmoothTransformation);
  }

  QByteArray pngData;
  QBuffer buffer(&pngData);
  if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) {
    return {};
  }

  return pngData;
}

} // namespace ArtworkUtils

#endif // ARTWORKUTILS_H

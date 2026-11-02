#include "coverartpresenter.h"

#include "songparser.h"
#include <QLabel>
#include <QSize>
#include <algorithm>

namespace {
QPixmap scaledCoverPixmap(const QPixmap &source, const QSize &logicalMaxSize,
                          qreal devicePixelRatio) {
  if (source.isNull() || logicalMaxSize.isEmpty()) {
    return {};
  }

  const qreal renderScale = std::max<qreal>(1.0, devicePixelRatio);
  const QSize physicalMaxSize = logicalMaxSize * renderScale;
  const QSize physicalTargetSize =
      source.size().scaled(physicalMaxSize, Qt::KeepAspectRatio);
  QPixmap scaled = source.scaled(physicalTargetSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
  scaled.setDevicePixelRatio(renderScale);
  return scaled;
}
} // namespace

CoverArtPresenter::CoverArtPresenter(QLabel *coverLabel, QObject *parent)
    : QObject(parent), coverLabel_(coverLabel) {}

void CoverArtPresenter::showSong(const MSong &song) {
  auto [data, size] = SongParser::extractCoverImage(song.at("filepath").text);
  if (size > 0) {
    cover_.loadFromData(data.data(), size);
    coverLabel_->clear();
    updateCoverSize();
    emit artworkChanged(QByteArray(reinterpret_cast<const char *>(data.data()),
                                   static_cast<int>(size)));
  } else {
    cover_ = QPixmap();
    coverLabel_->clear();
    coverLabel_->setText("No cover");
  }
}

void CoverArtPresenter::clear() {
  cover_ = QPixmap();
  coverLabel_->clear();
}

void CoverArtPresenter::updateCoverSize() {
  if (cover_.isNull()) {
    return;
  }

  const QSize logicalMaxSize = coverLabel_->contentsRect().size();
  const qreal devicePixelRatio = coverLabel_->devicePixelRatioF();
  coverLabel_->setPixmap(
      scaledCoverPixmap(cover_, logicalMaxSize, devicePixelRatio));
}

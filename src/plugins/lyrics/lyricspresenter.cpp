#include "lyricspresenter.h"

#include "iplayerhost.h"
#include "lyricsloader.h"
#include "lyricspanel.h"
#include <QApplication>
#include <QFont>
#include <QSettings>
#include <QStringList>

LyricsPresenter::LyricsPresenter(LyricsPanel *lyricsPanel, IPlayerHost &player)
    : QObject(lyricsPanel), lyricsPanel_(lyricsPanel), player_(player),
      lyricsManager_(this) {
  connect(&lyricsManager_, &LyricsManager::newLyricsLineIndex, lyricsPanel_,
          &LyricsPanel::updateLyricsPanel);
  player_.addListener(this);
}

LyricsPresenter::~LyricsPresenter() { player_.removeListener(this); }

void LyricsPresenter::trackStarted(const TrackInfo &track) {
  lyricsManager_.setLyrics(LyricsLoader::resolveLyrics(track));
  lyricsPanel_->setLyricsPanel(lyricsManager_.getCurrentLyricsMap());
}

void LyricsPresenter::stopped() {
  lyricsManager_.setLyrics({});
  lyricsPanel_->setLyricsPanel(lyricsManager_.getCurrentLyricsMap());
}

void LyricsPresenter::positionChanged(qint64 positionMs) {
  lyricsManager_.onPlayerProgressChange(positionMs);
}

void LyricsPresenter::applySettings() {
  QSettings settings;
  const QString fontFamily =
      settings.value("lyrics/font_family", QString()).toString();
  const bool useSystemDefaultFont =
      settings.value("lyrics/use_system_default_font", fontFamily.isEmpty())
          .toBool();
  const int defaultLyricsFontSize = QApplication::font().pointSize();
  const int fontSize =
      settings.value("lyrics/font_size", defaultLyricsFontSize).toInt();
  setFont(useSystemDefaultFont ? QString() : fontFamily, fontSize);

  QColor color(
      settings.value("lyrics/highlight_color", QString("#0064ff")).toString());
  if (!color.isValid()) {
    color = QColor(0, 100, 255);
  }
  setHighlightColor(color);
}

void LyricsPresenter::setFont(const QString &fontFamily, int pointSize) {
  // Start from the application font so its CJK fallback families are kept.
  QFont font = QApplication::font();
  font.setPointSize(pointSize);
  if (!fontFamily.isEmpty()) {
    QStringList families = font.families();
    families.removeAll(fontFamily);
    families.prepend(fontFamily);
    font.setFamilies(families);
  }
  lyricsPanel_->setLyricsTextFont(font);
}

void LyricsPresenter::setHighlightColor(const QColor &color) {
  lyricsPanel_->setHighlightTextColor(color);
}

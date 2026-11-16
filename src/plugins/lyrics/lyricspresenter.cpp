#include "lyricspresenter.h"

#include "iplayercontrol.h"
#include "iplayerhost.h"
#include "lyricsloader.h"
#include "lyricspanel.h"
#include "lyricssettings.h"
#include <QApplication>
#include <QFont>
#include <QStringList>

LyricsPresenter::LyricsPresenter(LyricsPanel *lyricsPanel, IPlayerHost &player,
                                 IPlayerControl &control)
    : QObject(lyricsPanel), lyricsPanel_(lyricsPanel), player_(player),
      control_(control), lyricsManager_(this) {
  connect(&lyricsManager_, &LyricsManager::newLyricsLineIndex, lyricsPanel_,
          &LyricsPanel::updateLyricsPanel);
  // Plays from the line: the seek first, so a paused song resumes at the
  // line, not where it was paused; while playing, play changes nothing. The
  // highlight moves with the seek's next position, not here
  // (docs/plugins/lyrics.md, "Seeking from a line, hover and the scroll
  // hold").
  connect(lyricsPanel_, &LyricsPanel::lineClicked, this, [this](int index) {
    control_.seek(lyricsManager_.lineStartMs(index));
    control_.play();
  });
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
  const LyricsSettings settings = loadLyricsSettings();
  setFont(settings.useSystemDefaultFont ? QString() : settings.fontFamily,
          settings.fontSize);
  setHighlightColor(settings.highlightColor);
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

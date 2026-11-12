#ifndef LYRICSPRESENTER_H
#define LYRICSPRESENTER_H

#include "iplayerlistener.h"
#include "lyricsmanager.h"
#include <QColor>
#include <QObject>
#include <QString>

class IPlayerHost;
class LyricsPanel;

// Shows the playing song's lyrics in a lyrics panel, highlights the line for
// the playback position, and applies the lyrics font and highlight color.
// A child of its panel: it listens to the player host for the panel's
// lifetime, and the panel is destroyed before the host.
class LyricsPresenter : public QObject, public IPlayerListener {
  Q_OBJECT

public:
  LyricsPresenter(LyricsPanel *lyricsPanel, IPlayerHost &player);
  ~LyricsPresenter() override;

  // Loads the song's lyrics (see LyricsLoader::resolveLyrics).
  void trackStarted(const TrackInfo &track) override;
  void stopped() override;
  // Highlights the lyrics line for the playback position.
  void positionChanged(qint64 positionMs) override;

  // Reads the lyrics font and highlight color from QSettings.
  void applySettings();
  // An empty fontFamily uses the application font family.
  void setFont(const QString &fontFamily, int pointSize);
  void setHighlightColor(const QColor &color);

private:
  LyricsPanel *lyricsPanel_;
  IPlayerHost &player_;
  LyricsManager lyricsManager_;
};

#endif // LYRICSPRESENTER_H

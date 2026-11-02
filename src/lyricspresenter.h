#ifndef LYRICSPRESENTER_H
#define LYRICSPRESENTER_H

#include "lyricsmanager.h"
#include "songlibrary.h"
#include <QColor>
#include <QObject>
#include <QString>
#include <string>
#include <unordered_map>

class LyricsPanel;

// Shows the current song's lyrics in the lyrics panel, highlights the line
// for the playback position, and applies the lyrics font and highlight color.
// Frontend class: MainWindow tells it when a song starts or stops and where
// playback is.
class LyricsPresenter : public QObject {
  Q_OBJECT

public:
  explicit LyricsPresenter(LyricsPanel *lyricsPanel, QObject *parent = nullptr);

  // Loads the song's lyrics (see LyricsLoader::resolveLyrics).
  // remainingFields are the tag fields from the refresh that have no column.
  void
  showSong(const MSong &song,
           const std::unordered_map<std::string, std::string> &remainingFields);
  void clear();
  // Highlights the lyrics line for the playback position.
  void setPositionMs(qint64 positionMs);

  // Reads the lyrics font and highlight color from QSettings.
  void applySettings();
  // An empty fontFamily uses the application font family.
  void setFont(const QString &fontFamily, int pointSize);
  void setHighlightColor(const QColor &color);

private:
  LyricsPanel *lyricsPanel_;
  LyricsManager lyricsManager_;
};

#endif // LYRICSPRESENTER_H

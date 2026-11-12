#ifndef LYRICSSETTINGS_H
#define LYRICSSETTINGS_H

#include <QColor>
#include <QString>

// The lyrics panel style, stored as the lyrics/* QSettings keys.
struct LyricsSettings {
  // Kept even while useSystemDefaultFont is set, so the page can show it.
  QString fontFamily;
  bool useSystemDefaultFont = true;
  int fontSize = 0;
  QColor highlightColor;
};

// Missing keys get the defaults: the system font at the application font's
// size, and a blue highlight.
LyricsSettings loadLyricsSettings();
void saveLyricsSettings(const LyricsSettings &settings);

#endif // LYRICSSETTINGS_H

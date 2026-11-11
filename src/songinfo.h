#ifndef SONGINFO_H
#define SONGINFO_H

#include <QString>
#include <vector>

class ColumnRegistry;
class MonitoredLibrary;
class SongLibrary;

// One label/value row of the Properties tab.
struct SongInfoRow {
  QString label;
  QString value;
  QString toolTip;
};

struct SongInfoSection {
  QString title;
  std::vector<SongInfoRow> rows;
};

// What the Properties tab shows for one song: display data (column titles,
// formatted values), so it is a frontend type. Empty (songPk < 0) for no song.
struct SongInfo {
  int songPk = -1;
  std::vector<SongInfoSection> sections;

  bool isEmpty() const { return songPk < 0; }
};

// Builds the Properties rows of the song: File (from one QFileInfo of its
// path, skipped when its watched folder is offline), Audio, Tags and Library
// (from the values stored in library). Returns an empty SongInfo for a song
// with no file path, which includes a song removed from library.
SongInfo buildSongInfo(int songPk, const SongLibrary &library,
                       const MonitoredLibrary &monitoredLibrary,
                       const ColumnRegistry &registry);

#endif // SONGINFO_H

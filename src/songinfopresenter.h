#ifndef SONGINFOPRESENTER_H
#define SONGINFOPRESENTER_H

#include <QObject>
#include <QPointer>
#include <QTimer>

class ColumnRegistry;
class MonitoredLibrary;
class Playlist;
class SongInfoPanel;
class SongLibrary;

// Shows the selected song in the Properties tab (SongInfoPanel). Frontend
// class: MainWindow tells it which song is selected
// (PlaylistTabs::selectedSongChanged); it rebuilds the panel when that song's
// data or a watched folder's availability changes. Rebuilds are debounced, so
// a run of selection changes (an arrow key held down, rows removed one by
// one) reads the file once.
class SongInfoPresenter : public QObject {
  Q_OBJECT

public:
  SongInfoPresenter(SongInfoPanel *panel, const SongLibrary &library,
                    const MonitoredLibrary &monitoredLibrary,
                    const ColumnRegistry &registry, QObject *parent = nullptr);

  // Shows songPk of playlist; songPk < 0, or no playlist, shows no song.
  void showSong(Playlist *playlist, int songPk);

private:
  static constexpr int kRebuildDelayMs = 100;

  void rebuild();
  void onSongDataChanged(int row);

  // The panel is an earlier child of the window, so it is deleted first.
  QPointer<SongInfoPanel> panel_;
  const SongLibrary &library_;
  const MonitoredLibrary &monitoredLibrary_;
  const ColumnRegistry &registry_;
  QPointer<Playlist> playlist_;
  int songPk_ = -1;
  QMetaObject::Connection playlistConnection_;
  // A child, so tests can find it.
  QTimer rebuildTimer_{this};
};

#endif // SONGINFOPRESENTER_H

#include "songinfopresenter.h"

#include "monitoredlibrary.h"
#include "playlist.h"
#include "songinfo.h"
#include "songinfopanel.h"

SongInfoPresenter::SongInfoPresenter(SongInfoPanel *panel,
                                     const SongLibrary &library,
                                     const MonitoredLibrary &monitoredLibrary,
                                     const ColumnRegistry &registry,
                                     QObject *parent)
    : QObject(parent), panel_(panel), library_(library),
      monitoredLibrary_(monitoredLibrary), registry_(registry) {
  rebuildTimer_.setSingleShot(true);
  rebuildTimer_.setInterval(kRebuildDelayMs);
  connect(&rebuildTimer_, &QTimer::timeout, this, &SongInfoPresenter::rebuild);
  // The song's folder may be the one that went offline or came back.
  connect(&monitoredLibrary_, &MonitoredLibrary::folderAvailabilityChanged,
          this, [this]() {
            if (songPk_ >= 0) {
              rebuildTimer_.start();
            }
          });
}

void SongInfoPresenter::showSong(Playlist *playlist, int songPk) {
  if (playlist != playlist_) {
    disconnect(playlistConnection_);
    playlist_ = playlist;
    if (playlist != nullptr) {
      playlistConnection_ = connect(playlist, &Playlist::songDataChanged, this,
                                    &SongInfoPresenter::onSongDataChanged);
    }
  }
  songPk_ = playlist != nullptr ? songPk : -1;
  rebuildTimer_.start();
}

void SongInfoPresenter::rebuild() {
  if (panel_) {
    // A song removed from the library meanwhile builds as no song.
    panel_->setInfo(
        buildSongInfo(songPk_, library_, monitoredLibrary_, registry_));
  }
}

// Every song write outside playlist operations (Song Properties, play stats,
// cloud sync, rescans) reaches the rows through
// PlaylistCollection::notifySongsChanged.
void SongInfoPresenter::onSongDataChanged(int row) {
  // Only the shown playlist is connected, and containsPk(-1) is false.
  if (playlist_->containsPk(songPk_) &&
      playlist_->getIndexByPk(songPk_) == row) {
    rebuildTimer_.start();
  }
}

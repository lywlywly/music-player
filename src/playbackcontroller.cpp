#include "playbackcontroller.h"

#include "playlist.h"
#include "playlistcollection.h"
#include <QDateTime>
#include <QDebug>
#include <QFileInfo>
#include <QRandomGenerator>
#include <algorithm>

PlaybackController::PlaybackController(SongLibrary &library,
                                       PlaylistCollection &playlists,
                                       QObject *parent)
    : QObject(parent), library_(library), playlists_(playlists) {
  skipFailedSongTimer_.setSingleShot(true);
  skipFailedSongTimer_.setInterval(0);
  connect(&skipFailedSongTimer_, &QTimer::timeout, this,
          &PlaybackController::next);
  connect(&playlists_, &PlaylistCollection::playlistAboutToBeRemoved, this,
          [this](int playlistId) {
            const Playlist *removed = playlists_.playlist(playlistId);
            // Removing the playing playlist stops playback; otherwise the
            // queue would drop the current song while audio keeps playing.
            if (queue_.getCurrentPk().second == removed) {
              stop();
            }
            queue_.removePlaylist(removed);
          });
  connect(&playlists_, &PlaylistCollection::songsRemovedFromLibrary, this,
          [this](const std::vector<int> &songPks) {
            if (std::find(songPks.begin(), songPks.end(), currentSongPk_) ==
                songPks.end()) {
              return;
            }
            // The current song no longer exists: do not keep playing it.
            // Reset first, so stop's listeners see no current song.
            currentSongPk_ = -1;
            if (manager_.getStatus() != PlaybackQueue::PlaybackStatus::None) {
              stop();
            }
          });
}

void PlaybackController::setShownPlaylist(
    std::function<Playlist *()> shownPlaylist) {
  shownPlaylist_ = std::move(shownPlaylist);
}

void PlaybackController::setPreferredNextRow(
    std::function<int()> preferredNextRow) {
  preferredNextRow_ = std::move(preferredNextRow);
}

void PlaybackController::setReady(bool ready) { ready_ = ready; }

void PlaybackController::connectPlayer() {
  connect(player(), &AudioPlayer::mediaStatusChanged, this,
          &PlaybackController::onMediaStatusChanged);
  connect(player(), &AudioPlayer::durationChanged, this,
          &PlaybackController::onDurationChanged);
  connect(player(), &AudioPlayer::positionChanged, this,
          &PlaybackController::onPositionChanged);
  connect(player(), &AudioPlayer::bitrateChanged, this,
          &PlaybackController::bitrateChanged);
}

Playlist *PlaybackController::shownPlaylist() const {
  return shownPlaylist_ ? shownPlaylist_() : nullptr;
}

void PlaybackController::play() {
  if (!canPlay()) {
    return;
  }
  if (manager_.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    Playlist *pl = shownPlaylist();
    if (pl == nullptr || pl->empty()) {
      return;
    }
    // Resume the last played song, or start from the top if it is not in
    // this playlist (new playlist, or the song was removed).
    const int lastPlayedPk = pl->getLastPlayed();
    const int row =
        pl->containsPk(lastPlayedPk) ? pl->getIndexByPk(lastPlayedPk) : 0;
    playSong(manager_.playIndex(row), row, pl);
    return;
  }
  player()->play();
  manager_.play();
  emit playbackStateChanged(PlaybackQueue::PlaybackStatus::Playing);
}

void PlaybackController::pause() {
  if (!canPlay() ||
      manager_.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    return;
  }
  player()->pause();
  manager_.pause();
  emit playbackStateChanged(PlaybackQueue::PlaybackStatus::Paused);
}

void PlaybackController::toggle() {
  if (manager_.getStatus() == PlaybackQueue::PlaybackStatus::Playing) {
    pause();
  } else {
    play();
  }
}

void PlaybackController::stop() {
  if (!canPlay()) {
    return;
  }
  skipFailedSongTimer_.stop();
  consecutiveFailures_ = 0;
  player()->stop();
  manager_.stop();
  emit playbackStateChanged(PlaybackQueue::PlaybackStatus::None);
}

void PlaybackController::next() {
  if (!canPlay()) {
    return;
  }
  const int preferredRow = preferredNextRow_ ? preferredNextRow_() : -1;
  const auto &[song, row, pl] = manager_.next(preferredRow);
  // Nothing to play (the shown playlist is empty): stop instead of leaving
  // the previous song playing with no current song.
  if (row < 0) {
    stop();
    return;
  }
  playSong(song, row, pl);
  emit movedToRow(row, pl);
}

void PlaybackController::prev() {
  if (!canPlay()) {
    return;
  }
  const auto &[song, row, pl] = manager_.prev();
  if (row < 0) {
    return;
  }
  playSong(song, row, pl);
  emit movedToRow(row, pl);
}

void PlaybackController::playRandom() {
  if (!canPlay()) {
    return;
  }
  Playlist *pl = shownPlaylist();
  if (pl == nullptr || pl->empty()) {
    return;
  }
  const int row = QRandomGenerator::global()->bounded(pl->songCount());
  playSong(manager_.playIndex(row), row, pl);
  emit movedToRow(row, pl);
}

void PlaybackController::playRow(int row) {
  if (!canPlay()) {
    return;
  }
  Playlist *pl = shownPlaylist();
  if (pl == nullptr) {
    return;
  }
  playSong(manager_.playIndex(row), row, pl);
}

void PlaybackController::seek(qint64 positionMs) {
  if (!canPlay()) {
    return;
  }
  completionTracker_.seeked();
  emit seeking(positionMs);
  player()->setPosition(positionMs);
}

void PlaybackController::setBackend(PlaybackBackendManager::Backend backend) {
  if (backendManager_ == nullptr) {
    backendManager_ = new PlaybackBackendManager(backend, this);
  } else if (backendManager_->currentBackend() == backend) {
    return;
  } else {
    stop();
    backendManager_->setBackend(backend);
  }
  connectPlayer();
  player()->setVolume(volumePercent_);
}

void PlaybackController::setVolume(int volumePercent) {
  volumePercent_ = volumePercent;
  if (backendManager_ != nullptr) {
    player()->setVolume(volumePercent_);
  }
}

void PlaybackController::playSong(const MSong &song, int row, Playlist *pl) {
  const int songPk = pl->getPkByIndex(row);
  // Copied: refreshing the song below may change song.
  const std::string filepath = song.at("filepath").text;
  if (filepath.empty()) {
    qFatal("playSong: filepath is empty");
  }
  const QString filepathText = QString::fromStdString(filepath);
  if (!QFileInfo::exists(filepathText)) {
    handlePlaybackFailure(filepathText);
    return;
  }
  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &activeSong =
      library_.refreshSongFromFile(filepath, &remainingFields);
  emit songAboutToStart(activeSong);
  // Set before setSource: InvalidMedia handling reads it (see
  // onMediaStatusChanged).
  currentSongPk_ = songPk;
  completionTracker_.startSession();
  library_.markSongPlayedAtStart(songPk, QDateTime::currentSecsSinceEpoch());
  playlists_.notifySongChanged(songPk);

  player()->setSource(QString::fromStdString(activeSong.at("filepath").text));
  player()->play();
  manager_.play();
  emit songStarted(activeSong, remainingFields);
}

void PlaybackController::handlePlaybackFailure(const QString &filepath) {
  qWarning() << "Cannot play file:" << filepath;
  // Like switching songs, the previous song stops right away. Otherwise its
  // position updates would reset the failure count and its EndOfMedia could
  // start another skip.
  player()->stop();
  ++consecutiveFailures_;
  const Playlist *pl = shownPlaylist();
  const int maxFailures = std::max(1, pl != nullptr ? pl->songCount() : 0);
  if (consecutiveFailures_ < maxFailures) {
    skipFailedSongTimer_.start();
    return;
  }
  stop();
  emit playbackFailed(filepath);
}

void PlaybackController::maybeCountCompletedPlay() {
  if (currentSongPk_ < 0 || !completionTracker_.shouldCount()) {
    return;
  }
  if (!library_.incrementPlayCount(currentSongPk_)) {
    return;
  }
  completionTracker_.markCounted();
  playlists_.notifySongChanged(currentSongPk_);
  emit playCounted(currentSongPk_);
}

void PlaybackController::onMediaStatusChanged(
    QMediaPlayer::MediaStatus status) {
  switch (status) {
  case QMediaPlayer::EndOfMedia:
    maybeCountCompletedPlay();
    emit songFinished();
    next();
    break;
  case QMediaPlayer::InvalidMedia:
    // InvalidMedia does not say which source failed. currentSongPk_ is the
    // song passed to setSource: playSong sets it before calling setSource,
    // so keep that order.
    if (manager_.getStatus() != PlaybackQueue::PlaybackStatus::None &&
        currentSongPk_ >= 0) {
      handlePlaybackFailure(QString::fromStdString(
          library_.getSongByPK(currentSongPk_).at("filepath").text));
    }
    break;
  default:
    break;
  }
}

void PlaybackController::onPositionChanged(qint64 positionMs) {
  // Playback is progressing, so the current song is playable.
  if (positionMs > 0) {
    consecutiveFailures_ = 0;
  }
  completionTracker_.recordPositionMs(
      positionMs,
      manager_.getStatus() == PlaybackQueue::PlaybackStatus::Playing);
  maybeCountCompletedPlay();
  emit positionChanged(positionMs);
}

void PlaybackController::onDurationChanged(qint64 durationMs) {
  completionTracker_.setDurationMs(durationMs);
  emit durationChanged(durationMs);
}

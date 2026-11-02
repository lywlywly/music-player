#ifndef PLAYBACKCONTROLLER_H
#define PLAYBACKCONTROLLER_H

#include "playbackbackendmanager.h"
#include "playbackmanager.h"
#include "playbackqueue.h"
#include "playcompletiontracker.h"
#include "songlibrary.h"
#include <QObject>
#include <QString>
#include <QTimer>
#include <functional>
#include <string>
#include <unordered_map>

class PlaylistCollection;

// Runs playback: the transport commands, starting a song on the audio
// backend, skipping songs that cannot be played, and counting completed
// plays. Backend class: it reports what happens through signals, and the
// window shows it.
//
// Owns the PlaybackQueue, the PlaybackManager on top of it, and the
// PlaybackBackendManager, which the first setBackend() creates. Transport
// commands do nothing until setReady(true) and setBackend() were both called,
// so requests that arrive while the library is loading are ignored.
class PlaybackController : public QObject {
  Q_OBJECT

public:
  PlaybackController(SongLibrary &library, PlaylistCollection &playlists,
                     QObject *parent = nullptr);

  PlaybackQueue &queue() { return queue_; }
  PlaybackManager &manager() { return manager_; }
  PlaybackQueue::PlaybackStatus status() { return manager_.getStatus(); }
  // The song most recently started (kept after stop), or -1 before any song
  // has played or after that song was removed from the library.
  int currentSongPk() const { return currentSongPk_; }
  // The song of currentSongPk(), or nullptr when it is -1.
  const MSong *currentSong() const;

  // The playlist the window shows: Play starts it when nothing is playing,
  // Random picks from it, and playback stops after as many consecutive
  // failures as it has songs. May return nullptr.
  void setShownPlaylist(std::function<Playlist *()> shownPlaylist);
  // The row Next plays when no song is queued (Playback follows cursor), or
  // -1. See PlaybackManager::next.
  void setPreferredNextRow(std::function<int()> preferredNextRow);
  void setReady(bool ready);

  // Transport commands.
  // With nothing playing, starts the shown playlist at its last played song,
  // or at its first row when that song is not in it; otherwise resumes.
  void play();
  void pause();
  void toggle();
  void stop();
  void next();
  void prev();
  // Plays a random row of the shown playlist (no shuffle history).
  void playRandom();
  // Plays row of the shown playlist, e.g. on a double-click.
  void playRow(int row);
  void seek(qint64 positionMs);

  // The first call creates the audio backend. A later call switches it if it
  // differs: stops playback (as stop()) and moves the volume to the new
  // player.
  void setBackend(PlaybackBackendManager::Backend backend);
  // 0..100. Kept for the backend setBackend() creates or switches to.
  void setVolume(int volumePercent);

signals:
  // A song is about to start, before its source is set (the backend may
  // report its duration and position during setSource).
  void songAboutToStart(const MSong &song);
  // The song started playing. remainingFields are the tag fields from the
  // refresh that have no column.
  void songStarted(
      const MSong &song,
      const std::unordered_map<std::string, std::string> &remainingFields);
  // Play resumed (Playing), pause (Paused) or stop (None). Not emitted when a
  // song starts; see songStarted.
  void playbackStateChanged(PlaybackQueue::PlaybackStatus status);
  // Next, Previous or Random (including a skip after a failure) moved
  // playback to row of playlist.
  void movedToRow(int row, Playlist *playlist);
  // A seek, before the backend is told.
  void seeking(qint64 positionMs);
  // The backend reached the end of the song, before Next.
  void songFinished();
  // The current song's play was counted (PlayCompletionTracker).
  void playCounted(int songPk);
  // Playback stopped because as many consecutive songs as the shown playlist
  // has could not be played; filepath is the last one.
  void playbackFailed(const QString &filepath);
  // From the current backend.
  void positionChanged(qint64 positionMs);
  void durationChanged(qint64 durationMs);
  void bitrateChanged(qint64 bitsPerSecond);

private:
  // Ready and a backend exists.
  bool canPlay() const { return ready_ && backendManager_ != nullptr; }
  void connectPlayer();
  AudioPlayer *player() const { return backendManager_->player(); }
  Playlist *shownPlaylist() const;
  void playSong(const MSong &song, int row, Playlist *playlist);
  // Called when a song cannot be played (missing file, or the backend reports
  // InvalidMedia). Skips to the next song, or stops and emits playbackFailed
  // once as many consecutive songs as the shown playlist holds have failed.
  void handlePlaybackFailure(const QString &filepath);
  void maybeCountCompletedPlay();
  void onMediaStatusChanged(QMediaPlayer::MediaStatus status);
  void onPositionChanged(qint64 positionMs);
  void onDurationChanged(qint64 durationMs);

  SongLibrary &library_;
  PlaylistCollection &playlists_;
  PlaybackQueue queue_;
  PlaybackManager manager_{queue_};
  // Child QObject; nullptr until the first setBackend().
  PlaybackBackendManager *backendManager_ = nullptr;
  PlayCompletionTracker completionTracker_;
  std::function<Playlist *()> shownPlaylist_;
  std::function<int()> preferredNextRow_;
  bool ready_ = false;
  int volumePercent_ = 100;
  // The song most recently passed to the backend by playSong, or -1.
  int currentSongPk_ = -1;
  int consecutiveFailures_ = 0;
  // Runs next() after a failure from the event loop rather than recursively:
  // the next() that hit the failure finishes first, so its movedToRow does
  // not override the skip's, and a long run of unplayable songs cannot
  // overflow the stack. stop() cancels a pending skip.
  QTimer skipFailedSongTimer_;
};

#endif // PLAYBACKCONTROLLER_H

#ifndef PLAYBACKQUEUE_H
#define PLAYBACKQUEUE_H

#include "playlist.h"
#include <QObject>
#include <deque>

// Current song, queued songs, and per-song playback status
class PlaybackQueue : public QObject {
  Q_OBJECT
public:
  enum class PlaybackStatus { Playing, Paused, None };
  enum class SongPlaybackState { None, Playing, Paused, Queued };
  struct SongStatus {
    SongPlaybackState state = SongPlaybackState::None;
    int queuePosition = -1;
  };

  explicit PlaybackQueue(QObject *parent = nullptr);
  void setCurrentId(int, Playlist *pl);
  void addNext(int, Playlist *pl);
  void addLast(int, Playlist *pl);
  const std::deque<int> &getQueue();
  std::pair<int, Playlist *> getCurrentPk() const;
  void setPlaybackStatus(PlaybackStatus);
  PlaybackStatus getStatus();
  std::pair<int, Playlist *> pop();
  bool empty() const;
  std::pair<int, Playlist *> getOrder(int);
  // Status of songPk as an entry of playlist. The current song reports its
  // playback status (queue position not consulted); otherwise a song queued
  // from the same playlist reports Queued with its position.
  SongStatus statusOf(int songPk, const Playlist *playlist) const;
  // Drops the current song and queued entries of a playlist that is about to
  // be destroyed. Playlists are only compared, never dereferenced.
  void removePlaylist(const Playlist *pl);

signals:
  void statusChanged(int songPk, Playlist *playlist);

private:
  void notifyAll(int, Playlist *);
  std::deque<int> queue;
  std::vector<std::pair<int, Playlist *>> orders;
  int currentPk = -1;
  Playlist *currentPlaylist = nullptr;
  PlaybackStatus playbackStatus = PlaybackStatus::None;
};

#endif // PLAYBACKQUEUE_H

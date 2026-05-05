#ifndef PLAYBACKMANAGER_H
#define PLAYBACKMANAGER_H

#include "playbackpolicy.h"
#include "playbackqueue.h"
#include "playlist.h"
#include <QObject>
#include <QUrl>
enum Policy { Sequential, Shuffle };

// Operations on song switching, abstract layer over `PlaybackQueue`
// call `setView(Playlist&)` with a valid playlist before using playback
// operations (`playIndex`, `next`, `prev`, queue operations) or changing
// policy.
class PlaybackManager : public QObject {
  Q_OBJECT
public:
  explicit PlaybackManager(PlaybackQueue &q, QObject *parent = nullptr);
  // PlayByIndex a song, make it current song, do not change queue
  const MSong &playIndex(int);
  // return const reference to a song, its index in playlist, and the playlist
  // that the song is in.
  std::tuple<const MSong &, int, Playlist *> next();
  // return const reference to a song, its index in playlist, and the playlist
  // that the song is in.
  std::tuple<const MSong &, int, Playlist *> prev();
  void play();
  void pause();
  void stop();
  // add to queue
  void queueEnd(int);
  void queueStart(int);
  // add to queue (cursor)
  void enqueueWeak(int);
  void setView(Playlist &);
  void setPolicy(Policy);
  Policy currentPolicy() const;
  PlaybackQueue::PlaybackStatus getStatus();

private:
  void syncPolicyContext();
  // const Playlist *playlist;
  Playlist *playlist = nullptr;
  PlaybackQueue &queue;
  std::unique_ptr<PlaybackPolicy> policy;
  Policy currentPolicy_ = Sequential;
  int candidateWeak = -1;
};

#endif // PLAYBACKMANAGER_H

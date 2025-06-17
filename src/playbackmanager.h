#ifndef PLAYBACKMANAGER_H
#define PLAYBACKMANAGER_H

#include "playbackpolicy.h"
#include "playbackqueue.h"
#include "playlist.h"
#include <QObject>
#include <QUrl>

enum Policy { Sequential, Shuffle };

class PlaybackManager : public QObject {
  Q_OBJECT
public:
  explicit PlaybackManager(PlaybackQueue &q, QObject *parent = nullptr);
  // play a song, make it current song, do not change queue
  const MSong &play(int);
  std::tuple<const MSong &, int> next();
  std::tuple<const MSong &, int> prev();
  void stop();
  // add to queue
  void queueEnd(int);
  void queueStart(int);
  // add to queue (cursor)
  void enqueueWeak(int);
  void setView(const Playlist *);
  void setPolicy(Policy);

private:
  const Playlist *pl;
  PlaybackQueue &queue;
  PlaybackPolicy *policy;
  int candidateWeak = -1;
};

#endif // PLAYBACKMANAGER_H

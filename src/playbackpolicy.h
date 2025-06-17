#ifndef PLAYBACKPOLICY_H
#define PLAYBACKPOLICY_H

#include "playbackqueue.h"
#include "playlist.h"
class PlaybackPolicy {
public:
  PlaybackPolicy(PlaybackQueue &);
  virtual void setPlaylist(const Playlist *) = 0;
  virtual int nextIndex() = 0;
  virtual int previousIndex() = 0;

private:
  virtual void reset() = 0;

protected:
  PlaybackQueue &queue;
  const Playlist *playlist;
};

#endif // PLAYBACKPOLICY_H

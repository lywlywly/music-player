#ifndef PLAYBACKPOLICYSEQUENTIAL_H
#define PLAYBACKPOLICYSEQUENTIAL_H

#include "playbackpolicy.h"
class PlaybackPolicySequential : public PlaybackPolicy {
public:
  PlaybackPolicySequential(PlaybackQueue &);

  // PlaybackPolicy interface
public:
  void setPlaylist(const Playlist *) override;
  int nextIndex() override;
  int previousIndex() override;

private:
  void reset() override;
};

#endif // PLAYBACKPOLICYSEQUENTIAL_H

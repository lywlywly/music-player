#ifndef PLAYBACKPOLICYSEQUENTIAL_H
#define PLAYBACKPOLICYSEQUENTIAL_H

#include "playbackpolicy.h"
class PlaybackPolicySequential : public PlaybackPolicy {
public:
  PlaybackPolicySequential();

  // PlaybackPolicy interface
public:
  void setPlaylist(const Playlist *) override;
  int nextPk() override;
  int prevPk() override;

private:
  void reset() override;
};

#endif // PLAYBACKPOLICYSEQUENTIAL_H

#include "playbackpolicysequential.h"

PlaybackPolicySequential::PlaybackPolicySequential() {}

void PlaybackPolicySequential::setPlaylist(const Playlist *playlist) {
  this->playlist = playlist;
}

int PlaybackPolicySequential::nextPk() {
  if (playlist->empty()) {
    return -1;
  }

  int curIdx =
      this->currentPk >= 0 ? playlist->getIndexByPk(this->currentPk) : -1;

  if (curIdx < playlist->songCount() - 1)
    this->currentPk = playlist->getPkByIndex(curIdx + 1);
  else
    this->currentPk = playlist->getPkByIndex(0);

  return this->currentPk;
}

int PlaybackPolicySequential::prevPk() {
  if (playlist->empty()) {
    return -1;
  }

  int curIdx =
      this->currentPk >= 0 ? playlist->getIndexByPk(this->currentPk) : 0;

  if (curIdx >= 1)
    this->currentPk = playlist->getPkByIndex(curIdx - 1);
  else
    this->currentPk = playlist->getPkByIndex(playlist->songCount() - 1);

  return this->currentPk;
}

void PlaybackPolicySequential::reset() {}

void PlaybackPolicySequential::setCurrentPk(int pk) { this->currentPk = pk; }

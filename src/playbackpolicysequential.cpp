#include "playbackpolicysequential.h"

PlaybackPolicySequential::PlaybackPolicySequential(PlaybackQueue &queue)
    : PlaybackPolicy{queue} {}

void PlaybackPolicySequential::setPlaylist(const Playlist *playlist) {
  this->playlist = playlist;
}

int PlaybackPolicySequential::nextIndex() {
  if (playlist->empty()) {
    return -1;
  }

  if (!queue.empty()) {
    return queue.pop();
  }

  int currPk = queue.getCurrentPk();
  int currentIndex = currPk >= 0 ? playlist->getIndexByPk(currPk) : -1;

  if (currentIndex < playlist->songCount() - 1) {
    return currentIndex + 1;
  } else {
    return 0;
  }

  throw std::runtime_error("never");
}

int PlaybackPolicySequential::previousIndex() {
  if (playlist->empty()) {
    return -1;
  }

  int currPk = queue.getCurrentPk();
  int currentIndex = currPk >= 0 ? playlist->getIndexByPk(currPk) : 0;

  if (currentIndex >= 1) {
    return currentIndex - 1;
  } else {
    return playlist->songCount() - 1;
  }

  throw std::runtime_error("never");
}

void PlaybackPolicySequential::reset() {}

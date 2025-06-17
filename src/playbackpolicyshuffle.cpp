#include "playbackpolicyshuffle.h"
#include <random>

PlaybackPolicyShuffle::PlaybackPolicyShuffle(PlaybackQueue &queue)
    : PlaybackPolicy{queue} {}

void PlaybackPolicyShuffle::setPlaylist(const Playlist *playlist) {
  this->playlist = playlist;
  recentPks =
      std::make_unique<BoundedSetWithHistory<int>>(playlist->songCount() / 2);
  // recentPks.reserve(playlist->songCount() / 2);
}

int PlaybackPolicyShuffle::nextIndex() {
  if (playlist->empty()) {
    return -1;
  }

  if (!queue.empty()) {
    return queue.pop();
  }

  if (recentPks->has_next()) {
    return *recentPks->next();
  }

  int ranIdx = getRandom(0, playlist->songCount() - 1);
  // retry if too often
  if (recentPks->contains(ranIdx) || queue.getCurrentPk() == ranIdx) {
    // `queue.getCurrentPk() == ranIdx` is used to check not current song by
    // directly double clicking
    return nextIndex();
  }

  recentPks->insert_back(ranIdx);

  return ranIdx;
}

int PlaybackPolicyShuffle::previousIndex() {
  if (playlist->empty()) {
    return -1;
  }

  if (recentPks->has_prev()) {
    return *recentPks->prev();
  }

  int ranIdx = getRandom(0, playlist->songCount() - 1);
  // retry if too often
  if (recentPks->contains(ranIdx) || queue.getCurrentPk() == ranIdx) {
    return previousIndex();
  }

  recentPks->insert_front(ranIdx);

  return ranIdx;
}

void PlaybackPolicyShuffle::reset() {}

int PlaybackPolicyShuffle::getRandom(int start, int end) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(start, end);

  int random_num = dist(gen);

  return random_num;
}

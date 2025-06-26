#include "playbackpolicyshuffle.h"
#include <random>

PlaybackPolicyShuffle::PlaybackPolicyShuffle() {}

void PlaybackPolicyShuffle::setPlaylist(const Playlist *playlist) {
  this->playlist = playlist;
  recentPks =
      std::make_unique<BoundedSetWithHistory<int>>(playlist->songCount() / 2);
}

int PlaybackPolicyShuffle::nextPk() {
  if (playlist->empty()) {
    return -1;
  }

  if (recentPks->has_next()) {
    return *recentPks->next();
  }

  int ranIdx = getRandom(0, playlist->songCount() - 1);
  int pk = playlist->getPkByIndex(ranIdx);
  // retry if too often
  if (recentPks->contains(pk)) {
    // `queue.getCurrentPk() == ranIdx` is used to check not current song by
    // directly double clicking
    // FIXME: only one song stackoverflow
    return nextPk();
  }

  recentPks->insert_back(pk);

  return pk;
}

int PlaybackPolicyShuffle::prevPk() {
  if (playlist->empty()) {
    return -1;
  }

  if (recentPks->has_prev()) {
    return *recentPks->prev();
  }

  int ranIdx = getRandom(0, playlist->songCount() - 1);
  int pk = playlist->getPkByIndex(ranIdx);
  // retry if too often
  if (recentPks->contains(pk)) {
    return prevPk();
  }

  recentPks->insert_front(pk);

  return pk;
}

void PlaybackPolicyShuffle::setCurrentPk(int pk) {
  recentPks->insert_cursor(pk);
}

void PlaybackPolicyShuffle::reset() {}

int PlaybackPolicyShuffle::getRandom(int start, int end) {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> dist(start, end);

  int random_num = dist(gen);

  return random_num;
}

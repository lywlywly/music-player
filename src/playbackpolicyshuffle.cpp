#include "playbackpolicyshuffle.h"
#include <random>

PlaybackPolicyShuffle::PlaybackPolicyShuffle() {}

PlaybackPolicyShuffle::~PlaybackPolicyShuffle() {
  playlist->unsetSizeChangeCallback();
}

void PlaybackPolicyShuffle::setPlaylist(const Playlist *playlist) {
  this->playlist = playlist;
  recentPks =
      std::make_unique<BoundedSetWithHistory<int>>(playlist->songCount() / 2);
  std::function<void(int)> statusUpdate = [this](int s) {
    this->recentPks->resize(s / 2);
  };
  playlist->setSizeChangeCallback(std::move(statusUpdate));
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

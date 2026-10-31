#include "playbackpolicyshuffle.h"
#include <random>

PlaybackPolicyShuffle::PlaybackPolicyShuffle() {}

PlaybackPolicyShuffle::~PlaybackPolicyShuffle() { disconnectPlaylist(); }

void PlaybackPolicyShuffle::setPlaylist(const Playlist *playlist) {
  disconnectPlaylist();
  this->playlist = playlist;
  recentPks =
      std::make_unique<BoundedSetWithHistory<int>>(playlist->songCount() / 2);
  const auto resizeHistory = [this]() {
    this->recentPks->resize(this->playlist->songCount() / 2);
  };
  playlistConnections_ = {
      QObject::connect(playlist, &Playlist::songsInserted, resizeHistory),
      QObject::connect(playlist, &Playlist::songsRemoved, resizeHistory),
      QObject::connect(playlist, &Playlist::songsReset, resizeHistory),
  };
}

void PlaybackPolicyShuffle::disconnectPlaylist() {
  for (const QMetaObject::Connection &connection : playlistConnections_) {
    QObject::disconnect(connection);
  }
  playlistConnections_.clear();
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

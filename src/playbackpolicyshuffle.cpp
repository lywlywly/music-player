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

  // History entries removed from the playlist are skipped.
  while (recentPks->has_next()) {
    const int pk = *recentPks->next();
    if (playlist->containsPk(pk)) {
      return pk;
    }
  }

  const int pk = randomPk();
  recentPks->insert_back(pk);
  return pk;
}

int PlaybackPolicyShuffle::prevPk() {
  if (playlist->empty()) {
    return -1;
  }

  while (recentPks->has_prev()) {
    const int pk = *recentPks->prev();
    if (playlist->containsPk(pk)) {
      return pk;
    }
  }

  const int pk = randomPk();
  recentPks->insert_front(pk);
  return pk;
}

int PlaybackPolicyShuffle::randomPk() {
  // Pick a random row whose song is not in the recent history. If every song
  // is recent (possible when rows repeat a song), pick from all rows.
  // TODO: once a playlist rejects duplicate rows, every song cannot be recent
  // (history holds half the rows), so the all-rows fallback can go.
  std::vector<int> candidates;
  for (int row = 0; row < playlist->songCount(); ++row) {
    const int pk = playlist->getPkByIndex(row);
    if (!recentPks->contains(pk)) {
      candidates.push_back(pk);
    }
  }
  if (candidates.empty()) {
    return playlist->getPkByIndex(getRandom(0, playlist->songCount() - 1));
  }
  return candidates[getRandom(0, static_cast<int>(candidates.size()) - 1)];
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

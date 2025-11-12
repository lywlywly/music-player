#include "playbackmanager.h"
#include "playbackpolicysequential.h"
#include "playbackpolicyshuffle.h"
#include <QDebug>

PlaybackManager::PlaybackManager(PlaybackQueue &q, QObject *parent)
    : queue{q}, QObject{parent} {}

const MSong &PlaybackManager::playIndex(int currentIndex) {
  int pk = playlist->getPkByIndex(currentIndex);
  policy->setCurrentPk(pk);
  queue.setCurrentId(pk, playlist);
  playlist->setLastPlayed(pk);
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);
  return playlist->getSongByPk(pk);
}

std::tuple<const MSong &, int, Playlist *> PlaybackManager::next() {
  int pk;
  Playlist *pl;

  if (!queue.empty()) {
    std::tie(pk, pl) = queue.pop();
    if (pl == playlist) {
      // if queued song not in current playlist, don't notify PlaybackPolicy
      policy->setCurrentPk(pk);
    }
  } else {
    pl = playlist;
    pk = policy->nextPk();
  }

  int row = pk < 0 ? -1 : pl->getIndexByPk(pk);
  queue.setCurrentId(pk, pl);
  playlist->setLastPlayed(pk);

  return {pl->getSongByPk(pk), row, pl};
}

std::tuple<const MSong &, int, Playlist *> PlaybackManager::prev() {
  int pk = policy->prevPk();

  if (pk < 0) {
    return {{}, -1, playlist};
  }

  queue.setCurrentId(pk, playlist);
  playlist->setLastPlayed(pk);

  return {playlist->getSongByPk(pk), playlist->getIndexByPk(pk), playlist};
}

void PlaybackManager::play() {
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);
}

void PlaybackManager::pause() {
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Paused);
}

void PlaybackManager::stop() {
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::None);
  queue.setCurrentId(-1, nullptr);
}

void PlaybackManager::queueEnd(int currentIndex) {
  int pk = playlist->getPkByIndex(currentIndex);
  queue.addLast(pk, playlist);
}

void PlaybackManager::queueStart(int currentIndex) {
  int pk = playlist->getPkByIndex(currentIndex);
  queue.addNext(pk, playlist);
}

void PlaybackManager::enqueueWeak(int i) { this->candidateWeak = i; }

void PlaybackManager::setView(Playlist *view) { this->playlist = view; }

void PlaybackManager::setPolicy(Policy policyEnum) {
  switch (policyEnum) {
  case Sequential:
    policy = std::make_unique<PlaybackPolicySequential>();
    break;
  case Shuffle:
    policy = std::make_unique<PlaybackPolicyShuffle>();
    break;
  }

  policy->setPlaylist(playlist);

  // notify PlaybackPolicy current song
  const auto &[curPk, curPl] = queue.getCurrentPk();
  if (curPl == playlist)
    policy->setCurrentPk(curPk);
}

PlaybackQueue::PlaybackStatus PlaybackManager::getStatus() {
  return queue.getStatus();
}

#include "playbackmanager.h"
#include "playbackpolicysequential.h"
#include "playbackpolicyshuffle.h"
#include <QtGlobal>

namespace {
const MSong &emptySongRef() {
  static const MSong emptySong;
  return emptySong;
}
} // namespace

PlaybackManager::PlaybackManager(PlaybackQueue &q, QObject *parent)
    : queue{q}, QObject{parent} {
  policy = std::make_unique<PlaybackPolicySequential>();
  currentPolicy_ = Sequential;
}

const MSong &PlaybackManager::playIndex(int currentIndex) {
  Q_ASSERT(playlist != nullptr);
  int pk = playlist->getPkByIndex(currentIndex);
  policy->setCurrentPk(pk);
  queue.setCurrentId(pk, playlist);
  playlist->setLastPlayed(pk);
  return playlist->getSongByPk(pk);
}

std::tuple<const MSong &, int, Playlist *>
PlaybackManager::activatePk(int pk, Playlist *pl) {
  if (pk < 0) {
    queue.setCurrentId(-1, nullptr);
    return {emptySongRef(), -1, pl};
  }

  const int row = pl->getIndexByPk(pk);
  queue.setCurrentId(pk, pl);
  playlist->setLastPlayed(pk);
  return {pl->getSongByPk(pk), row, pl};
}

std::tuple<const MSong &, int, Playlist *>
PlaybackManager::next(int preferredRow) {
  Q_ASSERT(playlist != nullptr);
  if (!queue.empty()) {
    auto [pk, pl] = queue.pop();
    // If queued song is not in current playlist, don't notify PlaybackPolicy.
    if (pl == playlist)
      policy->setCurrentPk(pk);
    return activatePk(pk, pl);
  }

  const bool preferredRowValid =
      preferredRow >= 0 && preferredRow < playlist->songCount();
  const int preferredPk =
      preferredRowValid ? playlist->getPkByIndex(preferredRow) : -1;
  const auto &[currentPk, currentPl] = queue.getCurrentPk();
  const bool preferredIsCurrent =
      preferredPk == currentPk && playlist == currentPl;
  if (preferredRowValid && preferredIsCurrent) {
    consumedPreferredPk_ = preferredPk;
  } else if (preferredRowValid && preferredPk != consumedPreferredPk_) {
    consumedPreferredPk_ = preferredPk;
    return {playIndex(preferredRow), preferredRow, playlist};
  }

  return activatePk(policy->nextPk(), playlist);
}

std::tuple<const MSong &, int, Playlist *> PlaybackManager::prev() {
  Q_ASSERT(playlist != nullptr);
  int pk = policy->prevPk();

  if (pk < 0) {
    return {emptySongRef(), -1, playlist};
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
  Q_ASSERT(playlist != nullptr);
  queue.addLast(playlist->getPkByIndex(currentIndex), playlist);
}

void PlaybackManager::queueStart(int currentIndex) {
  Q_ASSERT(playlist != nullptr);
  queue.addNext(playlist->getPkByIndex(currentIndex), playlist);
}

void PlaybackManager::enqueueWeak(int i) { candidateWeak = i; }

void PlaybackManager::setView(Playlist &view) {
  playlist = &view;
  syncPolicyContext();
}

void PlaybackManager::setPolicy(Policy policyEnum) {
  if (currentPolicy_ == policyEnum) {
    syncPolicyContext();
    return;
  }
  currentPolicy_ = policyEnum;

  switch (policyEnum) {
  case Sequential:
    policy = std::make_unique<PlaybackPolicySequential>();
    break;
  case Shuffle:
    policy = std::make_unique<PlaybackPolicyShuffle>();
    break;
  }
  syncPolicyContext();
}

Policy PlaybackManager::currentPolicy() const { return currentPolicy_; }

void PlaybackManager::syncPolicyContext() {
  Q_ASSERT(playlist != nullptr);
  policy->setPlaylist(playlist);
  const auto &[curPk, curPl] = queue.getCurrentPk();
  if (curPl == playlist) {
    policy->setCurrentPk(curPk);
  }
}

PlaybackQueue::PlaybackStatus PlaybackManager::getStatus() {
  return queue.getStatus();
}

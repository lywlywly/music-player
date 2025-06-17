#include "playbackmanager.h"
#include "playbackpolicysequential.h"
#include "playbackpolicyshuffle.h"
#include <QDebug>

PlaybackManager::PlaybackManager(PlaybackQueue &q, QObject *parent)
    : queue{q}, QObject{parent} {}

const MSong &PlaybackManager::play(int currentIndex) {
  int pk = pl->getPkByIndex(currentIndex);
  queue.setCurrentId(pk);
  return pl->getSongByPk(pk);
}

std::tuple<const MSong &, int> PlaybackManager::next() {
  int idx = policy->nextIndex();
  if (idx < 0) {
    return {{}, -1};
  }
  return {play(idx), idx};
}

std::tuple<const MSong &, int> PlaybackManager::prev() {
  int idx = policy->previousIndex();
  if (idx < 0) {
    return {{}, -1};
  }
  return {play(idx), idx};
}

void PlaybackManager::stop() { queue.setCurrentId(-1); }

void PlaybackManager::queueEnd(int currentIndex) {
  int pk = pl->getPkByIndex(currentIndex);
  queue.addLast(pk);
}

void PlaybackManager::queueStart(int currentIndex) {
  int pk = pl->getPkByIndex(currentIndex);
  queue.addNext(pk);
}

void PlaybackManager::enqueueWeak(int i) { this->candidateWeak = i; }

void PlaybackManager::setView(const Playlist *view) { this->pl = view; }

void PlaybackManager::setPolicy(Policy policyEnum) {
  switch (policyEnum) {
  case Sequential:
    policy = new PlaybackPolicySequential{queue};
    break;
  case Shuffle:
    policy = new PlaybackPolicyShuffle{queue};
    break;
  }

  policy->setPlaylist(pl);
}

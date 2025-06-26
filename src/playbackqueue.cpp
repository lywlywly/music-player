#include "playbackqueue.h"
#include <QDebug>

PlaybackQueue::PlaybackQueue(QObject *parent) : QObject{parent} {
  cbs.reserve(5);
}

void PlaybackQueue::setCurrentId(int curPk, Playlist *pl) {
  int oldPk = this->currentPk;
  this->currentPk = curPk;
  Playlist *oldpl = this->currentPlaylist;
  this->currentPlaylist = pl;
  notifyAll(curPk, pl);
  if (oldPk >= 0)
    notifyAll(oldPk, oldpl);
}

void PlaybackQueue::addNext(int pk, Playlist *pl) {
  queue.push_front(pk);
  if (orders.size() < pk + 1)
    orders.resize(pk + 1, {-1, nullptr});
  orders[pk] = {0, pl};
  for (auto i : queue) {
    orders[i].first += 1;
    notifyAll(i, orders[i].second);
  }
}

void PlaybackQueue::addLast(int pk, Playlist *pl) {
  queue.push_back(pk);
  if (orders.size() < pk + 1)
    orders.resize(pk + 1, {-1, nullptr});
  orders[pk] = {queue.size(), pl};
  notifyAll(pk, pl);
}

std::pair<int, Playlist *> PlaybackQueue::getOrder(int pk) {
  if (pk >= orders.size())
    return {-1, nullptr};
  return {orders[pk]};
}

void PlaybackQueue::notifyAll(int pk, Playlist *pl) {
  for (auto &cb : cbs) {
    cb(pk, pl);
  }
}

const std::deque<int> &PlaybackQueue::getQueue() { return queue; }

std::pair<int, Playlist *> PlaybackQueue::getCurrentPk() const {
  return {currentPk, currentPlaylist};
}

int PlaybackQueue::getStatus() { return status; }

std::pair<int, Playlist *> PlaybackQueue::pop() {
  if (queue.empty())
    throw std::out_of_range("deque is empty");

  int front = queue.front();
  Playlist *pl = orders[front].second;

  queue.erase(queue.begin());
  orders[front].first = -1;
  notifyAll(front, pl);

  for (int i : queue) {
    auto &[o, p] = orders[i];
    o -= 1;
    notifyAll(i, p);
  }

  return {front, pl};
}

bool PlaybackQueue::empty() const { return queue.empty(); }

void PlaybackQueue::setStatusUpdateCallback(StatusUpdateCallback &&cb) {
  cbs.push_back(std::move(cb));
}

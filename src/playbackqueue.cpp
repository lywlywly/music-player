#include "playbackqueue.h"
#include <QDebug>

PlaybackQueue::PlaybackQueue(QObject *parent) : QObject{parent} {}

void PlaybackQueue::setCurrentId(int currentId) {
  int oldId = this->currentId;
  this->currentId = currentId;
  if (cb)
    cb(currentId);
  if (oldId >= 0)
    if (cb)
      cb(oldId);
}

void PlaybackQueue::addNext(int pk) {
  queue.push_front(pk);
  if (orders.size() < pk + 1)
    orders.resize(pk + 1, -1);
  orders[pk] = 0;
  for (auto i : queue) {
    orders[i] += 1;
    if (cb)
      cb(i);
  }
}

void PlaybackQueue::addLast(int pk) {
  queue.push_back(pk);
  if (orders.size() < pk + 1)
    orders.resize(pk + 1, -1);
  orders[pk] = queue.size();
  if (cb)
    cb(pk);
}

int PlaybackQueue::getOrder(int pk) {
  if (pk >= orders.size())
    return -1;
  return orders[pk];
}

const std::deque<int> &PlaybackQueue::getQueue() { return queue; }

int PlaybackQueue::getCurrentPk() const { return currentId; }

int PlaybackQueue::getStatus() { return status; }

int PlaybackQueue::pop() {
  if (queue.empty())
    throw std::out_of_range("deque is empty");
  int front = queue.front();
  queue.erase(queue.begin());
  orders[front] = -1;
  if (cb)
    cb(front);
  for (auto i : queue) {
    orders[i] -= 1;
    if (cb)
      cb(i);
  }

  return front;
}

bool PlaybackQueue::empty() const { return queue.empty(); }

void PlaybackQueue::setStatusUpdateCallback(statusUpdateCallback &&_cb) {
  cb = std::move(_cb);
}

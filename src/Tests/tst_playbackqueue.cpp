#include <QObject>
#include <QTest>

#include "../playbackqueue.h"

class TestPlaybackQueue : public QObject {
  Q_OBJECT

private slots:
  void addNextAndAddLast_maintainsQueueAndOrder();
  void pop_updatesOrderAndReturnsFront();
  void setCurrentIdAndStatus_notifiesCallbacks();
  void pop_empty_throws();
};

void TestPlaybackQueue::addNextAndAddLast_maintainsQueueAndOrder() {
  PlaybackQueue queue;

  queue.addNext(1, nullptr);
  queue.addNext(2, nullptr);
  queue.addLast(3, nullptr);

  const std::deque<int> expected = {2, 1, 3};
  QCOMPARE(queue.getQueue(), expected);
  QCOMPARE(queue.getOrder(2).first, 1);
  QCOMPARE(queue.getOrder(1).first, 2);
  QCOMPARE(queue.getOrder(3).first, 3);
}

void TestPlaybackQueue::pop_updatesOrderAndReturnsFront() {
  PlaybackQueue queue;
  queue.addNext(1, nullptr);
  queue.addLast(2, nullptr);

  const auto popped = queue.pop();
  QCOMPARE(popped.first, 1);
  QVERIFY(queue.getQueue().size() == 1);
  QCOMPARE(queue.getQueue().front(), 2);
  QCOMPARE(queue.getOrder(1).first, -1);
  QCOMPARE(queue.getOrder(2).first, 1);
}

void TestPlaybackQueue::setCurrentIdAndStatus_notifiesCallbacks() {
  PlaybackQueue queue;
  std::vector<int> notified;
  queue.setStatusUpdateCallback([&](int pk, Playlist *) { notified.push_back(pk); });

  queue.setCurrentId(4, nullptr);
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);
  queue.setCurrentId(7, nullptr);

  const std::vector<int> expected = {4, 4, 7, 4};
  QCOMPARE(notified, expected);
  QCOMPARE(queue.getCurrentPk().first, 7);
  QCOMPARE(queue.getStatus(), PlaybackQueue::PlaybackStatus::Playing);
}

void TestPlaybackQueue::pop_empty_throws() {
  PlaybackQueue queue;
  QVERIFY_THROWS_EXCEPTION(std::out_of_range, queue.pop());
}

QTEST_MAIN(TestPlaybackQueue)
#include "tst_playbackqueue.moc"

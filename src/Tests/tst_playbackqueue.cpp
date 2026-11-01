#include <QObject>
#include <QTest>

#include "../playbackqueue.h"

namespace {
// Distinct fake addresses; statusOf only compares pointers.
Playlist *fakePlaylist(std::uintptr_t n) {
  return reinterpret_cast<Playlist *>(n * alignof(Playlist));
}
} // namespace

class TestPlaybackQueue : public QObject {
  Q_OBJECT

private slots:
  void addNextAndAddLast_maintainsQueueAndOrder();
  void pop_updatesOrderAndReturnsFront();
  void setCurrentIdAndStatus_notifiesStatusChanged();
  void statusOf_currentSongIgnoresQueuePosition();
  void statusOf_queuedSongInSamePlaylistOnly();
  void pop_empty_throws();
  void removePlaylist_dropsItsCurrentAndQueuedSongsOnly();
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

void TestPlaybackQueue::setCurrentIdAndStatus_notifiesStatusChanged() {
  PlaybackQueue queue;
  std::vector<int> notified;
  connect(&queue, &PlaybackQueue::statusChanged, this,
          [&](int pk, Playlist *) { notified.push_back(pk); });

  queue.setCurrentId(4, nullptr);
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);
  queue.setCurrentId(7, nullptr);

  const std::vector<int> expected = {4, 4, 7, 4};
  QCOMPARE(notified, expected);
  QCOMPARE(queue.getCurrentPk().first, 7);
  QCOMPARE(queue.getStatus(), PlaybackQueue::PlaybackStatus::Playing);
}

void TestPlaybackQueue::statusOf_currentSongIgnoresQueuePosition() {
  PlaybackQueue queue;
  Playlist *pl = fakePlaylist(1);
  queue.addLast(3, pl);
  queue.setCurrentId(3, pl);

  QCOMPARE(queue.statusOf(3, pl).state, PlaybackQueue::SongPlaybackState::None);
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);
  QCOMPARE(queue.statusOf(3, pl).state,
           PlaybackQueue::SongPlaybackState::Playing);
  queue.setPlaybackStatus(PlaybackQueue::PlaybackStatus::Paused);
  QCOMPARE(queue.statusOf(3, pl).state,
           PlaybackQueue::SongPlaybackState::Paused);
  QCOMPARE(queue.statusOf(3, pl).queuePosition, -1);
}

void TestPlaybackQueue::statusOf_queuedSongInSamePlaylistOnly() {
  PlaybackQueue queue;
  Playlist *plA = fakePlaylist(1);
  Playlist *plB = fakePlaylist(2);
  queue.setCurrentId(-1, nullptr);
  queue.addLast(2, plA);
  queue.addLast(5, plA);

  const PlaybackQueue::SongStatus queued = queue.statusOf(5, plA);
  QCOMPARE(queued.state, PlaybackQueue::SongPlaybackState::Queued);
  QCOMPARE(queued.queuePosition, 2);
  QCOMPARE(queue.statusOf(5, plB).state,
           PlaybackQueue::SongPlaybackState::None);
  QCOMPARE(queue.statusOf(9, plA).state,
           PlaybackQueue::SongPlaybackState::None);
}

void TestPlaybackQueue::pop_empty_throws() {
  PlaybackQueue queue;
  QVERIFY_THROWS_EXCEPTION(std::out_of_range, queue.pop());
}

void TestPlaybackQueue::removePlaylist_dropsItsCurrentAndQueuedSongsOnly() {
  PlaybackQueue queue;
  Playlist *plA = fakePlaylist(1);
  Playlist *plB = fakePlaylist(2);
  queue.setCurrentId(1, plA);
  queue.addLast(2, plA);
  queue.addLast(3, plB);

  queue.removePlaylist(plA);

  QCOMPARE(queue.getCurrentPk().first, -1);
  QCOMPARE(queue.getCurrentPk().second, nullptr);
  const std::deque<int> expected = {3};
  QCOMPARE(queue.getQueue(), expected);
  QCOMPARE(queue.statusOf(2, plA).state,
           PlaybackQueue::SongPlaybackState::None);
  QCOMPARE(queue.statusOf(3, plB).queuePosition, 1);
}

QTEST_MAIN(TestPlaybackQueue)
#include "tst_playbackqueue.moc"

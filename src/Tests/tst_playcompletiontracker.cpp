#include <QObject>
#include <QTest>

#include "../playcompletiontracker.h"

namespace {
// Feeds playing positions from fromMs to toMs (inclusive) in stepMs steps.
void playThrough(PlayCompletionTracker &tracker, qint64 fromMs, qint64 toMs,
                 qint64 stepMs = 1000) {
  for (qint64 ms = fromMs; ms <= toMs; ms += stepMs) {
    tracker.recordPositionMs(ms, true);
  }
}
} // namespace

class TestPlayCompletionTracker : public QObject {
  Q_OBJECT

private slots:
  void noDuration_neverCounts();
  void fullListen_countsOnceAtNearEnd();
  void reachingEndBySeeking_doesNotCount();
  void pausedPositions_doNotAddListenedTime();
  void shortSong_nearEndIsFiveSecondsBeforeEnd();
  void startSession_resetsProgressAndKeepsDuration();
};

void TestPlayCompletionTracker::noDuration_neverCounts() {
  PlayCompletionTracker tracker;
  tracker.startSession();
  playThrough(tracker, 0, 200000);
  QVERIFY(!tracker.shouldCount());
}

void TestPlayCompletionTracker::fullListen_countsOnceAtNearEnd() {
  PlayCompletionTracker tracker;
  tracker.startSession();
  tracker.setDurationMs(100000);

  playThrough(tracker, 0, 89000);
  QVERIFY(!tracker.shouldCount()); // two thirds listened, 90% not reached
  tracker.recordPositionMs(90000, true);
  QVERIFY(tracker.shouldCount());

  tracker.markCounted();
  QVERIFY(!tracker.shouldCount());
  playThrough(tracker, 91000, 100000);
  QVERIFY(!tracker.shouldCount());
}

void TestPlayCompletionTracker::reachingEndBySeeking_doesNotCount() {
  PlayCompletionTracker tracker;
  tracker.startSession();
  tracker.setDurationMs(100000);

  playThrough(tracker, 0, 10000);
  tracker.seeked();
  playThrough(tracker, 95000, 100000);
  QVERIFY(!tracker.shouldCount());
}

void TestPlayCompletionTracker::pausedPositions_doNotAddListenedTime() {
  PlayCompletionTracker tracker;
  tracker.startSession();
  tracker.setDurationMs(100000);

  for (qint64 ms = 0; ms <= 100000; ms += 1000) {
    tracker.recordPositionMs(ms, false);
  }
  QVERIFY(!tracker.shouldCount());
}

void TestPlayCompletionTracker::shortSong_nearEndIsFiveSecondsBeforeEnd() {
  PlayCompletionTracker tracker;
  tracker.startSession();
  tracker.setDurationMs(30000); // 90% = 27 s, end - 5 s = 25 s

  playThrough(tracker, 0, 24000);
  QVERIFY(!tracker.shouldCount());
  tracker.recordPositionMs(25000, true);
  QVERIFY(tracker.shouldCount());
}

void TestPlayCompletionTracker::startSession_resetsProgressAndKeepsDuration() {
  PlayCompletionTracker tracker;
  tracker.startSession();
  tracker.setDurationMs(100000);
  playThrough(tracker, 0, 80000);

  // Same song again: the backend may not report the unchanged duration.
  tracker.startSession();
  tracker.recordPositionMs(90000, true);
  QVERIFY(!tracker.shouldCount()); // previous session's listening is gone
  tracker.startSession();
  playThrough(tracker, 0, 90000);
  QVERIFY(tracker.shouldCount());
}

QTEST_MAIN(TestPlayCompletionTracker)
#include "tst_playcompletiontracker.moc"

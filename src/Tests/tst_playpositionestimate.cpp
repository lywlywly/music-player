#include <QObject>
#include <QTest>
#include <cstdint>

#include "../playpositionestimate.h"

class TestPlayPositionEstimate : public QObject {
  Q_OBJECT

private slots:
  void advancesOnlyWhilePlaying();
  void reportOrStateChangeRestartsFromNow();
};

void TestPlayPositionEstimate::advancesOnlyWhilePlaying() {
  PlayPositionEstimate estimate;
  QCOMPARE(estimate.positionUs(500), int64_t{0});

  // Stopped: the reported position, however long since.
  estimate.setPosition(2000, 1000);
  QCOMPARE(estimate.positionUs(1500), int64_t{2'000'000});

  // Playing: the report plus the time since, also past the next report's
  // due time (the reports stopped).
  estimate.setPlaying(true, 1000);
  QCOMPARE(estimate.positionUs(1040), int64_t{2'040'000});
  QCOMPARE(estimate.positionUs(1600), int64_t{2'600'000});

  // Paused: held at the last report.
  estimate.setPlaying(false, 1100);
  QCOMPARE(estimate.positionUs(1900), int64_t{2'000'000});
}

void TestPlayPositionEstimate::reportOrStateChangeRestartsFromNow() {
  PlayPositionEstimate estimate;
  estimate.setPlaying(true, 0);
  estimate.setPosition(5000, 100);
  QCOMPARE(estimate.positionUs(150), int64_t{5'050'000});

  // A new report (a seek's, say) replaces the estimate.
  estimate.setPosition(1000, 200);
  QCOMPARE(estimate.positionUs(230), int64_t{1'030'000});

  // Resuming counts from the resume, not from the report before the pause.
  estimate.setPlaying(false, 300);
  estimate.setPlaying(true, 900);
  QCOMPARE(estimate.positionUs(950), int64_t{1'050'000});
}

QTEST_GUILESS_MAIN(TestPlayPositionEstimate)
#include "tst_playpositionestimate.moc"

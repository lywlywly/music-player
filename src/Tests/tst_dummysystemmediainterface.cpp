#include <QObject>
#include <QSignalSpy>
#include <QTest>

#include "../dummysystemmediainterface.h"

class TestDummySystemMediaInterface : public QObject {
  Q_OBJECT

private slots:
  void setTitleAndArtist_updatesState();
  void setters_updateState();
  void requestHelpers_emitRequestSignals();
};

void TestDummySystemMediaInterface::setTitleAndArtist_updatesState() {
  DummySystemMediaInterface media;
  media.setArtwork(QByteArray("cover"));
  media.setPosition(123);

  media.setTitleAndArtist("Title A", "Artist A");

  const auto &state = media.stateForTest();
  QCOMPARE(state.title, QString("Title A"));
  QCOMPARE(state.artist, QString("Artist A"));
  QCOMPARE(state.positionMs, 0LL);
  QCOMPARE(state.playbackState, ISystemMediaInterface::PlaybackState::Playing);
  QVERIFY(state.artworkData.isEmpty());
}

void TestDummySystemMediaInterface::setters_updateState() {
  DummySystemMediaInterface media;

  media.setPlaybackState(ISystemMediaInterface::PlaybackState::Paused);
  media.setDuration(5000);
  media.setPosition(1200);
  media.updateCurrentPosition(1300);
  media.setArtwork(QByteArray("abc"));

  const auto &state = media.stateForTest();
  QCOMPARE(state.playbackState, ISystemMediaInterface::PlaybackState::Paused);
  QCOMPARE(state.durationMs, 5000LL);
  QCOMPARE(state.positionMs, 1300LL);
  QCOMPARE(state.artworkData.size(), 3);
}

void TestDummySystemMediaInterface::requestHelpers_emitRequestSignals() {
  DummySystemMediaInterface media;
  QSignalSpy playSpy(&media, &ISystemMediaInterface::playRequested);
  QSignalSpy pauseSpy(&media, &ISystemMediaInterface::pauseRequested);
  QSignalSpy toggleSpy(&media, &ISystemMediaInterface::toggleRequested);
  QSignalSpy nextSpy(&media, &ISystemMediaInterface::nextRequested);
  QSignalSpy prevSpy(&media, &ISystemMediaInterface::previousRequested);
  QSignalSpy seekSpy(&media, &ISystemMediaInterface::seekRequested);

  media.requestPlayForTest();
  media.requestPauseForTest();
  media.requestToggleForTest();
  media.requestNextForTest();
  media.requestPreviousForTest();
  media.requestSeekForTest(777);

  QCOMPARE(playSpy.count(), 1);
  QCOMPARE(pauseSpy.count(), 1);
  QCOMPARE(toggleSpy.count(), 1);
  QCOMPARE(nextSpy.count(), 1);
  QCOMPARE(prevSpy.count(), 1);
  QCOMPARE(seekSpy.count(), 1);
  QCOMPARE(seekSpy.first().at(0).toLongLong(), 777LL);
}

QTEST_MAIN(TestDummySystemMediaInterface)
#include "tst_dummysystemmediainterface.moc"

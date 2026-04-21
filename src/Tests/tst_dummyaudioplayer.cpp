#include <QObject>
#include <QSignalSpy>
#include <QTest>
#include <QUrl>

#include "../dummyaudioplayer.h"

class TestDummyAudioPlayer : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void setSource_nonEmpty_emitsDurationPositionAndLoaded();
  void setSource_empty_emitsNoMedia();
  void setPosition_emitsPositionChanged();
  void stop_emitsPositionResetAndNoMedia();
  void play_emitsExpectedStatus_data();
  void play_emitsExpectedStatus();
};

void TestDummyAudioPlayer::initTestCase() {
  qRegisterMetaType<QMediaPlayer::MediaStatus>("QMediaPlayer::MediaStatus");
}

void TestDummyAudioPlayer::setSource_nonEmpty_emitsDurationPositionAndLoaded() {
  DummyAudioPlayer player;
  player.setDurationForTest(125000);
  QSignalSpy durationSpy(&player, &AudioPlayer::durationChanged);
  QSignalSpy positionSpy(&player, &AudioPlayer::positionChanged);
  QSignalSpy statusSpy(&player, &AudioPlayer::mediaStatusChanged);

  player.setSource(QUrl::fromLocalFile("/tmp/dummy-a.wav"));

  QCOMPARE(durationSpy.count(), 1);
  QCOMPARE(durationSpy.first().at(0).toLongLong(), 125000LL);
  QCOMPARE(positionSpy.count(), 1);
  QCOMPARE(positionSpy.first().at(0).toLongLong(), 0LL);
  QCOMPARE(statusSpy.count(), 1);
  QCOMPARE(qvariant_cast<QMediaPlayer::MediaStatus>(statusSpy.first().at(0)),
           QMediaPlayer::LoadedMedia);
}

void TestDummyAudioPlayer::setSource_empty_emitsNoMedia() {
  DummyAudioPlayer player;
  QSignalSpy durationSpy(&player, &AudioPlayer::durationChanged);
  QSignalSpy positionSpy(&player, &AudioPlayer::positionChanged);
  QSignalSpy statusSpy(&player, &AudioPlayer::mediaStatusChanged);

  player.setSource(QUrl{});

  QCOMPARE(durationSpy.count(), 0);
  QCOMPARE(positionSpy.count(), 0);
  QCOMPARE(statusSpy.count(), 1);
  QCOMPARE(qvariant_cast<QMediaPlayer::MediaStatus>(statusSpy.first().at(0)),
           QMediaPlayer::NoMedia);
}

void TestDummyAudioPlayer::setPosition_emitsPositionChanged() {
  DummyAudioPlayer player;
  QSignalSpy positionSpy(&player, &AudioPlayer::positionChanged);

  player.setPosition(321);

  QCOMPARE(positionSpy.count(), 1);
  QCOMPARE(positionSpy.first().at(0).toLongLong(), 321LL);
}

void TestDummyAudioPlayer::stop_emitsPositionResetAndNoMedia() {
  DummyAudioPlayer player;
  player.setSource(QUrl::fromLocalFile("/tmp/dummy-b.wav"));

  QSignalSpy positionSpy(&player, &AudioPlayer::positionChanged);
  QSignalSpy statusSpy(&player, &AudioPlayer::mediaStatusChanged);
  positionSpy.clear();
  statusSpy.clear();

  player.stop();

  QCOMPARE(positionSpy.count(), 1);
  QCOMPARE(positionSpy.first().at(0).toLongLong(), 0LL);
  QCOMPARE(statusSpy.count(), 1);
  QCOMPARE(qvariant_cast<QMediaPlayer::MediaStatus>(statusSpy.first().at(0)),
           QMediaPlayer::NoMedia);
}

void TestDummyAudioPlayer::play_emitsExpectedStatus_data() {
  QTest::addColumn<bool>("hasSource");
  QTest::addColumn<QMediaPlayer::MediaStatus>("expectedStatus");

  QTest::newRow("without-source") << false << QMediaPlayer::NoMedia;
  QTest::newRow("with-source") << true << QMediaPlayer::LoadedMedia;
}

void TestDummyAudioPlayer::play_emitsExpectedStatus() {
  QFETCH(bool, hasSource);
  QFETCH(QMediaPlayer::MediaStatus, expectedStatus);

  DummyAudioPlayer player;
  if (hasSource) {
    player.setSource(QUrl::fromLocalFile("/tmp/dummy-c.wav"));
  }

  QSignalSpy statusSpy(&player, &AudioPlayer::mediaStatusChanged);
  player.play();

  QCOMPARE(statusSpy.count(), 1);
  QCOMPARE(qvariant_cast<QMediaPlayer::MediaStatus>(statusSpy.first().at(0)),
           expectedStatus);
}

QTEST_MAIN(TestDummyAudioPlayer)
#include "tst_dummyaudioplayer.moc"

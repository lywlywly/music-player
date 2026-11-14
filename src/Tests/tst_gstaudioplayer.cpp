#include <QAudioDevice>
#include <QMediaDevices>
#include <QObject>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

#include "../audioplayer.h"
#include "../playbackbackendmanager.h"
#include "../spectrum/pcmtap.h"
#include "../spectrum/spectrumanalyzer.h"
#include "testaudiofiles.h"

// Plays a file through the real GStreamer player (pipeline, ReplayGain and
// the PCM probe), at volume 0. It opens the default audio output, so it is
// skipped on a machine without one.
class TestGstAudioPlayer : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void playing24BitAudio_fillsTheTapAtThePosition();
  void replayGain_appliesTheModesGain();
};

void TestGstAudioPlayer::initTestCase() {
  if (QMediaDevices::audioOutputs().isEmpty()) {
    QSKIP("No audio output device.");
  }
}

void TestGstAudioPlayer::playing24BitAudio_fillsTheTapAtThePosition() {
  // 24-bit: the audio filter passes it on as S24LE.
  QTemporaryDir dir;
  const QString path = dir.filePath("tone.wav");
  QVERIFY(writeToneWav(path, 1170.8, 3.0)); // the center of the 1.2K band

  // The manager runs the GLib loop the player needs on macOS and Windows.
  PlaybackBackendManager backend(PlaybackBackendManager::Backend::GStreamer);
  AudioPlayer *player = backend.player();
  qint64 positionMs = 0;
  connect(player, &AudioPlayer::positionChanged, this,
          [&positionMs](qint64 ms) { positionMs = ms; });
  const auto tap = std::make_shared<PcmTap>();
  player->setPcmTap(tap);
  player->setVolume(0);
  // A plain path, as PlaybackController passes it.
  player->setSource(QUrl(path));
  player->play();

  SpectrumAnalyzer analyzer(SpectrumBands::logSpaced(50.0, 20000.0, 20));
  std::vector<float> samples(analyzer.fftSize());
  const auto readsTheToneAt = [&](qint64 ms) {
    if (tap->read(ms * 1000, samples) != 44100) {
      return false;
    }
    const std::vector<float> levels = analyzer.analyze(samples, 44100);
    const auto loudest = std::max_element(levels.begin(), levels.end());
    return loudest - levels.begin() == 10 && std::abs(*loudest + 6.0f) < 1.0f;
  };
  // The decoder runs ahead of the position, so the tap holds the audio
  // there (from 200 ms, a whole window).
  QTRY_VERIFY(positionMs >= 200 && readsTheToneAt(positionMs));

  // After a seek, the stream times follow the new position.
  player->setPosition(2000);
  QTRY_VERIFY(readsTheToneAt(2200));

  player->stop();
  QCOMPARE(tap->read(2'200'000, samples), 0);
}

void TestGstAudioPlayer::replayGain_appliesTheModesGain() {
  // The tone is at -6 dB, tagged with -4 dB track and -10 dB album gain; the
  // tap, after the ReplayGain volume, reads it with the gain.
  QTemporaryDir dir;
  const QString path = dir.filePath("tagged.wav");
  QVERIFY(writeToneWavWithReplayGain(path, 1170.8, 10.0, -4.0, -10.0));

  PlaybackBackendManager backend(PlaybackBackendManager::Backend::GStreamer);
  AudioPlayer *player = backend.player();
  qint64 positionMs = 0;
  connect(player, &AudioPlayer::positionChanged, this,
          [&positionMs](qint64 ms) { positionMs = ms; });
  const auto tap = std::make_shared<PcmTap>();
  player->setPcmTap(tap);
  player->setVolume(0);

  SpectrumAnalyzer analyzer(SpectrumBands::logSpaced(50.0, 20000.0, 20));
  std::vector<float> samples(analyzer.fftSize());
  const auto readsTheToneAt = [&](float db) {
    if (positionMs < 200 || tap->read(positionMs * 1000, samples) != 44100) {
      return false;
    }
    const std::vector<float> levels = analyzer.analyze(samples, 44100);
    const auto loudest = std::max_element(levels.begin(), levels.end());
    return loudest - levels.begin() == 10 && std::abs(*loudest - db) < 1.0f;
  };

  // The mode set before the song applies from its start.
  player->setReplayGainMode(ReplayGainMode::Album);
  player->setSource(QUrl(path));
  player->play();
  QTRY_VERIFY(readsTheToneAt(-16.0f));

  // Every switch applies while the song plays, from the tags stored at its
  // start.
  player->setReplayGainMode(ReplayGainMode::Off);
  QTRY_VERIFY(readsTheToneAt(-6.0f));
  player->setReplayGainMode(ReplayGainMode::Track);
  QTRY_VERIFY(readsTheToneAt(-10.0f));
  player->setReplayGainMode(ReplayGainMode::Album);
  QTRY_VERIFY(readsTheToneAt(-16.0f));

  // The next song starts from its own tags: an untagged tone at -6 dB.
  const QString untagged = dir.filePath("untagged.wav");
  QVERIFY(writeToneWav(untagged, 1170.8, 3.0));
  player->stop();
  positionMs = 0;
  player->setSource(QUrl(untagged));
  player->play();
  QTRY_VERIFY(readsTheToneAt(-6.0f));
  player->stop();
}

QTEST_GUILESS_MAIN(TestGstAudioPlayer)
#include "tst_gstaudioplayer.moc"

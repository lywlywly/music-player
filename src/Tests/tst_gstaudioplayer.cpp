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

// Plays a file through the real GStreamer player (pipeline, rgvolume and the
// PCM probe), at volume 0. It opens the default audio output, so it is
// skipped on a machine without one.
class TestGstAudioPlayer : public QObject {
  Q_OBJECT

private slots:
  void initTestCase();
  void playing24BitAudio_fillsTheTapAtThePosition();
};

void TestGstAudioPlayer::initTestCase() {
  if (QMediaDevices::audioOutputs().isEmpty()) {
    QSKIP("No audio output device.");
  }
}

void TestGstAudioPlayer::playing24BitAudio_fillsTheTapAtThePosition() {
  // 24-bit: rgvolume passes it on as S24LE, not the F32LE or S16LE it
  // advertises.
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

QTEST_GUILESS_MAIN(TestGstAudioPlayer)
#include "tst_gstaudioplayer.moc"

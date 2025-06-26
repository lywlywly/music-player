#include "qtaudioplayer.h"
#include <qaudiooutput.h>

QtAudioPlayer::QtAudioPlayer(QObject *parent) {
  connect(&p, &QMediaPlayer::durationChanged, this,
          &QtAudioPlayer::durationChanged);
  connect(&p, &QMediaPlayer::positionChanged, this,
          &QtAudioPlayer::positionChanged);
  connect(&p, &QMediaPlayer::mediaStatusChanged, this,
          &QtAudioPlayer::mediaStatusChanged);

  auto audioOutput = new QAudioOutput;
  p.setAudioOutput(audioOutput);
  // audioOutput->setVolume(50);
}

void QtAudioPlayer::play() { p.play(); }

void QtAudioPlayer::pause() { p.pause(); }

void QtAudioPlayer::setSource(const QUrl &source) { p.setSource(source); }

void QtAudioPlayer::setPosition(qint64 milliseconds) {
  p.setPosition(milliseconds);
}

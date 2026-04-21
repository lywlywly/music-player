#include "dummyaudioplayer.h"

DummyAudioPlayer::DummyAudioPlayer(QObject *parent) : AudioPlayer(parent) {}

void DummyAudioPlayer::play() {
  if (source_.isEmpty()) {
    emit mediaStatusChanged(QMediaPlayer::NoMedia);
    return;
  }
  emit mediaStatusChanged(QMediaPlayer::LoadedMedia);
}

void DummyAudioPlayer::pause() {}

void DummyAudioPlayer::stop() {
  positionMs_ = 0;
  emit positionChanged(positionMs_);
  emit mediaStatusChanged(QMediaPlayer::NoMedia);
}

void DummyAudioPlayer::setSource(const QUrl &source) {
  source_ = source;
  positionMs_ = 0;
  if (source_.isEmpty()) {
    emit mediaStatusChanged(QMediaPlayer::NoMedia);
    return;
  }
  emit durationChanged(durationMs_);
  emit positionChanged(positionMs_);
  emit mediaStatusChanged(QMediaPlayer::LoadedMedia);
}

void DummyAudioPlayer::setPosition(qint64 position) {
  positionMs_ = position;
  emit positionChanged(positionMs_);
}

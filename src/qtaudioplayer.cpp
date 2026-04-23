#include "qtaudioplayer.h"

QtAudioPlayer::QtAudioPlayer(QObject *parent) : AudioPlayer(parent) {
  connect(&mediaDevices_, &QMediaDevices::audioOutputsChanged, this,
          [this]() { handleAudioOutputsChanged(); });

  createPlaybackObjects();
}

void QtAudioPlayer::play() {
  if (player_) {
    player_->play();
  }
}

void QtAudioPlayer::pause() {
  if (player_) {
    player_->pause();
  }
}

void QtAudioPlayer::stop() {
  if (player_) {
    player_->stop();
    player_->setSource(QUrl{});
  }
}

void QtAudioPlayer::setSource(const QUrl &source) {
  if (player_) {
    player_->setSource(source);
  }
}

void QtAudioPlayer::setPosition(qint64 position) {
  if (player_) {
    player_->setPosition(position);
  }
}

void QtAudioPlayer::createPlaybackObjects() {
  player_ = std::make_unique<QMediaPlayer>();
  player_->setParent(this);

  audioOutput_ = new QAudioOutput(this);
  audioOutput_->setDevice(QMediaDevices::defaultAudioOutput());
  audioOutput_->setVolume(1.0);
  player_->setAudioOutput(audioOutput_);

  connect(player_.get(), &QMediaPlayer::durationChanged, this,
          &QtAudioPlayer::durationChanged);
  connect(player_.get(), &QMediaPlayer::positionChanged, this,
          [this](qint64 positionMs) { emit positionChanged(positionMs); });
  connect(player_.get(), &QMediaPlayer::mediaStatusChanged, this,
          &QtAudioPlayer::mediaStatusChanged);
}

void QtAudioPlayer::handleAudioOutputsChanged() {
  if (!audioOutput_) {
    return;
  }

  audioOutput_->setDevice(QMediaDevices::defaultAudioOutput());
}

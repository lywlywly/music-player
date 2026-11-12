#include "qtaudioplayer.h"
#include "audiotaps.h"
#include "spectrum/pcmtap.h"

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
  clearPcmTap();
}

void QtAudioPlayer::setSource(const QUrl &source) {
  if (player_) {
    player_->setSource(source);
  }
  clearPcmTap();
}

void QtAudioPlayer::setPosition(qint64 position) {
  clearPcmTap();
  if (player_) {
    player_->setPosition(position);
  }
}

void QtAudioPlayer::setVolume(int volumePercent) {
  AudioPlayer::setVolume(volumePercent);
  if (audioOutput_) {
    audioOutput_->setVolume(static_cast<float>(this->volumePercent()) / 100.0f);
  }
}

void QtAudioPlayer::setPcmTap(std::shared_ptr<PcmTap> tap) {
  // Attached only while there is a tap, so the backend does not copy
  // buffers for nothing.
  player_->setAudioBufferOutput(tap ? bufferOutput_ : nullptr);
  AudioPlayer::setPcmTap(std::move(tap));
}

void QtAudioPlayer::createPlaybackObjects() {
  player_ = std::make_unique<QMediaPlayer>();
  player_->setParent(this);

  audioOutput_ = new QAudioOutput(this);
  audioOutput_->setDevice(QMediaDevices::defaultAudioOutput());
  audioOutput_->setVolume(static_cast<float>(this->volumePercent()) / 100.0f);
  player_->setAudioOutput(audioOutput_);

  // No format requested: buffers come in the decoded format. Direct: push in
  // the thread that sends the buffer; PcmTap and pcmTap() are thread-safe.
  bufferOutput_ = new QAudioBufferOutput(this);
  connect(
      bufferOutput_, &QAudioBufferOutput::audioBufferReceived, this,
      [this](const QAudioBuffer &buffer) {
        if (const std::shared_ptr<PcmTap> tap = pcmTap()) {
          pushAudioBuffer(buffer, *tap);
        }
      },
      Qt::DirectConnection);

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

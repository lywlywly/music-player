#include "pluginaudiohost.h"

#include "pcmtap.h"
#include "playbackcontroller.h"

PluginAudioHost::PluginAudioHost(PlaybackController &playback, QObject *parent)
    : QObject(parent), playback_(playback), tap_(std::make_shared<PcmTap>()) {
  clock_.start();
  connect(&playback, &PlaybackController::positionChanged, this,
          [this](qint64 positionMs) {
            estimate_.setPosition(positionMs, clock_.elapsed());
          });
  // A song start sends songStarted, not playbackStateChanged.
  connect(&playback, &PlaybackController::songStarted, this,
          [this]() { estimate_.setPlaying(true, clock_.elapsed()); });
  connect(&playback, &PlaybackController::playbackStateChanged, this,
          [this](PlaybackQueue::PlaybackStatus status) {
            estimate_.setPlaying(status ==
                                     PlaybackQueue::PlaybackStatus::Playing,
                                 clock_.elapsed());
          });
}

void PluginAudioHost::addUser() {
  if (users_++ == 0) {
    playback_.setPcmTap(tap_);
  }
}

void PluginAudioHost::removeUser() {
  if (--users_ == 0) {
    // Withdrawn first, so the player does not refill it; cleared, so the next
    // user starts from new audio.
    playback_.setPcmTap(nullptr);
    tap_->clear();
  }
}

int PluginAudioHost::read(std::span<float> out) const {
  return tap_->read(estimate_.positionUs(clock_.elapsed()), out);
}

#include "audioplayer.h"
#include "spectrum/pcmtap.h"
#include <algorithm>

AudioPlayer::AudioPlayer(QObject *parent) : QObject{parent} {}

void AudioPlayer::setVolume(int volumePercent) {
  volumePercent_ = std::clamp(volumePercent, 0, 100);
}

int AudioPlayer::volumePercent() const { return volumePercent_; }

void AudioPlayer::setPcmTap(std::shared_ptr<PcmTap> tap) {
  std::lock_guard lock(pcmTapMutex_);
  pcmTap_ = std::move(tap);
}

std::shared_ptr<PcmTap> AudioPlayer::pcmTap() const {
  std::lock_guard lock(pcmTapMutex_);
  return pcmTap_;
}

void AudioPlayer::clearPcmTap() {
  if (const std::shared_ptr<PcmTap> tap = pcmTap()) {
    tap->clear();
  }
}

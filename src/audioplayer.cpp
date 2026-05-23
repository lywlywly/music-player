#include "audioplayer.h"
#include <algorithm>

AudioPlayer::AudioPlayer(QObject *parent) : QObject{parent} {}

void AudioPlayer::setVolume(int volumePercent) {
  volumePercent_ = std::clamp(volumePercent, 0, 100);
}

int AudioPlayer::volumePercent() const { return volumePercent_; }

#include "playpositionestimate.h"

void PlayPositionEstimate::setPosition(int64_t positionMs, int64_t nowMs) {
  positionMs_ = positionMs;
  sinceMs_ = nowMs;
}

void PlayPositionEstimate::setPlaying(bool playing, int64_t nowMs) {
  playing_ = playing;
  // The position is reported again soon; until then, count from now.
  sinceMs_ = nowMs;
}

int64_t PlayPositionEstimate::positionUs(int64_t nowMs) const {
  const int64_t ms = playing_ ? positionMs_ + (nowMs - sinceMs_) : positionMs_;
  return ms * 1000;
}

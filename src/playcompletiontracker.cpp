#include "playcompletiontracker.h"

#include <algorithm>

void PlayCompletionTracker::startSession() {
  maxPositionMs_ = 0;
  listenedMs_ = 0;
  lastPositionSampleMs_ = -1;
  counted_ = false;
}

void PlayCompletionTracker::setDurationMs(qint64 durationMs) {
  durationMs_ = durationMs;
}

void PlayCompletionTracker::seeked() { lastPositionSampleMs_ = -1; }

void PlayCompletionTracker::recordPositionMs(qint64 positionMs, bool playing) {
  maxPositionMs_ = std::max(maxPositionMs_, positionMs);
  if (playing && lastPositionSampleMs_ >= 0) {
    const qint64 delta = positionMs - lastPositionSampleMs_;
    if (delta > 0) {
      listenedMs_ += delta;
    }
  }
  lastPositionSampleMs_ = positionMs;
}

bool PlayCompletionTracker::shouldCount() const {
  if (counted_ || durationMs_ <= 0) {
    return false;
  }
  const qint64 nearEndThreshold =
      std::max<qint64>(0, std::min((durationMs_ * 9) / 10, durationMs_ - 5000));
  const qint64 listenedThreshold = (durationMs_ * 2) / 3;
  return maxPositionMs_ >= nearEndThreshold && listenedMs_ >= listenedThreshold;
}

void PlayCompletionTracker::markCounted() { counted_ = true; }

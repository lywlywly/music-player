#ifndef PLAYPOSITIONESTIMATE_H
#define PLAYPOSITIONESTIMATE_H

#include <cstdint>

// The play position between the player's reports, which come only every
// 100 ms or so: the last reported position plus the time since, while
// playing. Times are from a monotonic clock, passed in. If the reports stop
// while playing, the estimate keeps advancing (docs/plugins/plugin-system.md,
// "Audio for plugins").
class PlayPositionEstimate {
public:
  void setPosition(int64_t positionMs, int64_t nowMs);
  // A report or a state change restarts the estimate from nowMs; it advances
  // only while playing.
  void setPlaying(bool playing, int64_t nowMs);
  int64_t positionUs(int64_t nowMs) const;

private:
  int64_t positionMs_ = 0;
  // The time of the last report or state change.
  int64_t sinceMs_ = 0;
  bool playing_ = false;
};

#endif // PLAYPOSITIONESTIMATE_H

#ifndef PLAYCOMPLETIONTRACKER_H
#define PLAYCOMPLETIONTRACKER_H

#include <QtGlobal>

// Decides when a play of the current song counts as completed, from the
// player's position and duration updates.
//
// A play counts once the song reached 90% of its duration or 5 s before its
// end, whichever comes first, and at least two thirds of its duration were
// listened to while playing. Seeking does not add listened time.
//
// Only the listening session is kept here. The caller owns which song is
// playing, counts the play (SongLibrary::incrementPlayCount) and then calls
// markCounted(), so a session is counted at most once.
class PlayCompletionTracker {
public:
  // A new song started. The duration is kept until setDurationMs(): the
  // backend may not report a duration again when it is unchanged.
  void startSession();
  void setDurationMs(qint64 durationMs);
  // The next position sample after a seek does not add listened time.
  void seeked();
  void recordPositionMs(qint64 positionMs, bool playing);

  // True once the session meets the thresholds, until markCounted().
  bool shouldCount() const;
  void markCounted();

private:
  qint64 durationMs_ = 0;
  qint64 maxPositionMs_ = 0;
  qint64 listenedMs_ = 0;
  qint64 lastPositionSampleMs_ = -1;
  bool counted_ = false;
};

#endif // PLAYCOMPLETIONTRACKER_H

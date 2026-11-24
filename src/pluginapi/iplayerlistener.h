#ifndef IPLAYERLISTENER_H
#define IPLAYERLISTENER_H

#include "trackinfo.h"
#include <QtGlobal>

// Playback events for a plugin, which adds itself to IPlayerHost. Each method
// does nothing by default, so a listener overrides only the events it uses.
// Called on the main thread, synchronously; a method must not add or remove
// listeners.
class IPlayerListener {
public:
  virtual ~IPlayerListener() = default;

  // A song started playing. A song start sends this, not resumed().
  virtual void trackStarted(const TrackInfo & /*track*/) {}
  // Playback stopped.
  virtual void stopped() {}
  // The playing song was paused.
  virtual void paused() {}
  // The paused song plays again.
  virtual void resumed() {}
  // The playback position of the current song.
  virtual void positionChanged(qint64 /*positionMs*/) {}
  // The duration of the current song. The backend may report it while the
  // song is set up, before trackStarted.
  virtual void durationChanged(qint64 /*durationMs*/) {}
  // A seek to positionMs, before the backend is told.
  virtual void seeked(qint64 /*positionMs*/) {}
  // The song played to its end, before the next one starts.
  virtual void finished() {}
};

#endif // IPLAYERLISTENER_H

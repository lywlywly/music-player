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

  // A song started playing.
  virtual void trackStarted(const TrackInfo & /*track*/) {}
  // Playback stopped.
  virtual void stopped() {}
  // The playback position of the current song.
  virtual void positionChanged(qint64 /*positionMs*/) {}
};

#endif // IPLAYERLISTENER_H

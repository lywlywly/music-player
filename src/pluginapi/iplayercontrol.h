#ifndef IPLAYERCONTROL_H
#define IPLAYERCONTROL_H

#include <QtGlobal>

// Playback commands for plugins (docs/event-ordering.md, "Plugin commands:
// the contract"). A command returns at once: the host queues it and runs it
// in a later turn of the main thread's event loop, so code right after the
// call still sees the old state. Commands run in the order they were called,
// every one of them (none are merged). One may be called inside a listener
// call, and from any thread. Its effect arrives through the listener events
// once it has run; there is no return value, and a command without effect
// reports nothing. Commands still queued when the host is destroyed are
// dropped.
class IPlayerControl {
public:
  virtual ~IPlayerControl() = default;

  // Seeks the playing song to positionMs. Not bound to a song: it applies to
  // whatever plays when it runs, so a seek called just before a song change
  // lands in the next song.
  virtual void seek(qint64 positionMs) = 0;
};

#endif // IPLAYERCONTROL_H

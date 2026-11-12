#ifndef IPLAYERHOST_H
#define IPLAYERHOST_H

class IPlayerListener;

// The host's playback, for plugins. The owner of a listener adds it and
// removes it before the listener is destroyed; listeners are called in the
// order they were added.
class IPlayerHost {
public:
  virtual ~IPlayerHost() = default;

  virtual void addListener(IPlayerListener *listener) = 0;
  virtual void removeListener(IPlayerListener *listener) = 0;
};

#endif // IPLAYERHOST_H

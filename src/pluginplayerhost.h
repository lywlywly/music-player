#ifndef PLUGINPLAYERHOST_H
#define PLUGINPLAYERHOST_H

#include "iplayerhost.h"
#include <QObject>
#include <vector>

class IPlayerListener;
class PlaybackController;

// The playback host for plugins: forwards PlaybackController's song start,
// stop and position to the listeners, with the song as a TrackInfo (plugins
// see no app types; docs/plugins.md). Frontend class.
class PluginPlayerHost : public QObject, public IPlayerHost {
  Q_OBJECT

public:
  explicit PluginPlayerHost(PlaybackController &playback,
                            QObject *parent = nullptr);

  void addListener(IPlayerListener *listener) override;
  void removeListener(IPlayerListener *listener) override;

private:
  std::vector<IPlayerListener *> listeners_;
};

#endif // PLUGINPLAYERHOST_H

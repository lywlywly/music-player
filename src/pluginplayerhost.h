#ifndef PLUGINPLAYERHOST_H
#define PLUGINPLAYERHOST_H

#include "iplayercontrol.h"
#include "iplayerhost.h"
#include <QObject>
#include <vector>

class IPlayerListener;
class PlaybackController;

// The playback host for plugins: forwards PlaybackController's song start,
// stop, pause, resume, position, duration, seeks and song ends to the
// listeners, with the song as a TrackInfo (plugins see no app types;
// docs/plugins/plugin-system.md), and queues the plugins' playback commands.
// Frontend class.
class PluginPlayerHost : public QObject,
                         public IPlayerHost,
                         public IPlayerControl {
  Q_OBJECT

public:
  explicit PluginPlayerHost(PlaybackController &playback,
                            QObject *parent = nullptr);

  void addListener(IPlayerListener *listener) override;
  void removeListener(IPlayerListener *listener) override;

  void seek(qint64 positionMs) override;
  void play() override;

private:
  PlaybackController &playback_;
  std::vector<IPlayerListener *> listeners_;
};

#endif // PLUGINPLAYERHOST_H

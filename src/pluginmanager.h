#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "ipluginhost.h"
#include "pluginplayerhost.h"
#include <QList>
#include <QObject>
#include <QString>

class PlaybackController;

// Loads the plugins in a folder and is their host (docs/plugins.md).
// Frontend class. A plugin's root object is one per process, so one manager
// exists at a time.
class PluginManager : public QObject, public IPluginHost {
  Q_OBJECT

public:
  // Loads and initializes every plugin in folder, in file name order, and
  // ignores files that are not libraries. A library that is not a plugin of
  // this core API version, or a plugin whose id is already loaded, is skipped
  // and logged.
  PluginManager(PlaybackController &playback, const QString &folder,
                QObject *parent = nullptr);
  // Shuts the plugins down. They stay loaded: their code must stay mapped
  // while objects they created live.
  ~PluginManager() override;

  IPlayerHost &player() override;

  // The plugins' root objects, in load order; the places using a capability
  // qobject_cast them to it.
  const QList<QObject *> &plugins() const;

private:
  // Destroyed after the destructor's shutdown calls, so the plugins' listeners
  // are gone before it.
  PluginPlayerHost player_;
  QList<QObject *> plugins_;
};

#endif // PLUGINMANAGER_H

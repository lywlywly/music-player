#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "ipluginhost.h"
#include "pluginplayerhost.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class PlaybackController;

// A library in the plugins folder, and what the manager did with it
// (docs/plugins.md, "PluginManager").
struct PluginEntry {
  enum class State { Loaded, Disabled, Skipped };

  // For example lyricsplugin.dll.
  QString fileName;
  // From the plugin's metadata; empty when it could not be read.
  QString id;
  QString name;
  State state = State::Skipped;
  // Why it was skipped, in a few words, and in full (the text logged).
  QString reason;
  QString details;
  // The plugin's root object when Loaded, else null.
  QObject *root = nullptr;
};

// The ids of the plugins the user disabled (plugins/disabled). Only the
// manager, at construction, and the Plugins page read and write it.
QStringList loadDisabledPluginIds();
void saveDisabledPluginIds(const QStringList &ids);

// Loads the plugins in a folder and is their host (docs/plugins.md).
// Frontend class. A plugin's root object is one per process, so one manager
// exists at a time.
class PluginManager : public QObject, public IPluginHost {
  Q_OBJECT

public:
  // Lists every library in folder, in file name order, and ignores files that
  // are not libraries. Loads and initializes each one that is a plugin of
  // this core API version, has an id and a name, is not disabled and has an id
  // no earlier loaded or disabled entry has; the others are listed as
  // Disabled or Skipped, and a skip is logged.
  PluginManager(PlaybackController &playback, const QString &folder,
                QObject *parent = nullptr);
  // Shuts the loaded plugins down. They stay loaded: their code must stay
  // mapped while objects they created live.
  ~PluginManager() override;

  IPlayerHost &player() override;

  // An entry per library, in name order; the places using a capability
  // qobject_cast the Loaded entries' root objects to it.
  const QList<PluginEntry> &plugins() const;

private:
  // Destroyed after the destructor's shutdown calls, so the plugins' listeners
  // are gone before it.
  PluginPlayerHost player_;
  QList<PluginEntry> plugins_;
};

#endif // PLUGINMANAGER_H

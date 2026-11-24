#ifndef PLUGINMANAGER_H
#define PLUGINMANAGER_H

#include "pluginplayerhost.h"
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>
#include <memory>
#include <vector>

class ColumnRegistry;
class PlaybackController;
class PluginHost;
class SongLibrary;

// A library in the plugins folder, and what the manager did with it
// (docs/plugins/plugin-system.md, "PluginManager").
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

// Loads the plugins in a folder and is their host
// (docs/plugins/plugin-system.md). Frontend class. A plugin's root object is
// one per process, so one manager exists at a time.
class PluginManager : public QObject {
  Q_OBJECT

public:
  // Lists every library in folder, in file name order, and ignores files that
  // are not libraries. Loads and initializes each one that is a plugin of
  // this core API version, has an id and a name, is not disabled and has an id
  // no earlier loaded or disabled entry has; the others are listed as
  // Disabled or Skipped, and a skip is logged. Each loaded plugin gets a host
  // of its own, whose data folder is <dataRoot>/<id>.
  PluginManager(PlaybackController &playback, const QString &folder,
                const QString &dataRoot, QObject *parent = nullptr);
  // Shuts the loaded plugins down. They stay loaded: their code must stay
  // mapped while objects they created live.
  ~PluginManager() override;

  // An entry per library, in name order; the places using a capability
  // qobject_cast the Loaded entries' root objects to it.
  const QList<PluginEntry> &plugins() const;

  // Registers the fields of each loaded ISongFieldProvider, in load order,
  // into registry and gives library its source (docs/plugins/playstats.md,
  // "Registration"). A provider whose field ids clash with a field or with
  // an earlier provider's, or whose key fields are not built-in tag fields,
  // is refused and logged. Call once, before the library loads.
  void registerSongFields(ColumnRegistry &registry, SongLibrary &library);

signals:
  // A plugin's ISongFieldHost::valuesChanged, on the main thread, after the
  // call's queued hop.
  void providedValuesChanged(const QString &pluginId, const QStringList &keys);

private:
  // Destroyed after the destructor's shutdown calls, so the plugins' listeners
  // are gone before it.
  PluginPlayerHost player_;
  // One per loaded plugin, destroyed after the shutdown calls; each is the
  // context of its plugin's queued valuesChanged calls, which are dropped
  // with it.
  std::vector<std::unique_ptr<PluginHost>> hosts_;
  QList<PluginEntry> plugins_;
};

#endif // PLUGINMANAGER_H

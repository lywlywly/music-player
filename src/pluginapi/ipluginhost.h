#ifndef IPLUGINHOST_H
#define IPLUGINHOST_H

#include <QString>

class IPlayerControl;
class IPlayerHost;
class ISongFieldHost;

// The host's services, for one plugin: each plugin gets its own. A plugin
// gets it in IPlugin::initialize and must not use it after IPlugin::shutdown.
// Called on the main thread.
class IPluginHost {
public:
  virtual ~IPluginHost() = default;

  virtual IPlayerHost &player() = 0;
  virtual IPlayerControl &control() = 0;
  // The plugin's own folder for its data files, under the app's data
  // folder. The path only: the plugin creates the folder when it first
  // writes there.
  virtual QString dataFolder() const = 0;
  // For a plugin that provides song fields (ISongFieldProvider).
  virtual ISongFieldHost &songFieldHost() = 0;
};

#endif // IPLUGINHOST_H

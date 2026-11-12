#ifndef IPLUGINHOST_H
#define IPLUGINHOST_H

class IPlayerHost;

// The host's services, for plugins. A plugin gets it in IPlugin::initialize
// and must not use it after IPlugin::shutdown.
class IPluginHost {
public:
  virtual ~IPluginHost() = default;

  virtual IPlayerHost &player() = 0;
};

#endif // IPLUGINHOST_H

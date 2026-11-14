#ifndef IPLUGIN_H
#define IPLUGIN_H

#include <QObject>

class IPluginHost;

// A plugin's root object implements this, and the capabilities it provides
// (IPanelProvider, ...). Its IID is the version of the core API: IPlugin,
// IPluginHost, IPlayerHost, IPlayerListener, TrackInfo and the metadata keys;
// changing any of them bumps it (docs/plugins.md, "Versions").
//
// The plugin declares its metadata in a JSON file given to
// Q_PLUGIN_METADATA(IID IPlugin_iid FILE "<name>.json"), which the host reads
// without loading the plugin's code: "id" (stable; names the plugin's info
// panel tab, its settings keys <id>/... and its entry in the disabled list) and
// "name" (shown in Settings > Plugins), both non-empty strings.
class IPlugin {
public:
  virtual ~IPlugin() = default;

  // Called once, after loading, before any capability is used.
  virtual void initialize(IPluginHost &host) = 0;
  // Called once, before the host is destroyed. Afterwards the plugin must
  // not use the host, and has removed every listener it added.
  virtual void shutdown() = 0;
};

#define IPlugin_iid "myplayer.IPlugin/2"
Q_DECLARE_INTERFACE(IPlugin, IPlugin_iid)

#endif // IPLUGIN_H

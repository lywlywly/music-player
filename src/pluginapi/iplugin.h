#ifndef IPLUGIN_H
#define IPLUGIN_H

#include <QObject>
#include <QString>

class IPluginHost;

// A plugin's root object implements this, and the capabilities it provides
// (IPanelProvider, ...). Its IID is the version of the core API: IPlugin,
// IPluginHost, IPlayerHost, IPlayerListener and TrackInfo; changing any of
// them bumps it (docs/plugins.md, "Versions").
class IPlugin {
public:
  virtual ~IPlugin() = default;

  // Stable; names the plugin in logs, its info panel tab and its settings
  // keys (<id>/...).
  virtual QString id() const = 0;
  // Called once, after loading, before any capability is used.
  virtual void initialize(IPluginHost &host) = 0;
  // Called once, before the host is destroyed. Afterwards the plugin must
  // not use the host, and has removed every listener it added.
  virtual void shutdown() = 0;
};

#define IPlugin_iid "myplayer.IPlugin/1"
Q_DECLARE_INTERFACE(IPlugin, IPlugin_iid)

#endif // IPLUGIN_H

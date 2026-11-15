#ifndef ABREPEATPLUGIN_H
#define ABREPEATPLUGIN_H

#include "imenuprovider.h"
#include "iplugin.h"
#include <QObject>
#include <memory>

class ABRepeat;
class IPluginHost;

// The A-B Repeat plugin's root object: the loop, following the host's
// playback for the plugin's lifetime, and its submenu of Playback.
class ABRepeatPlugin : public QObject, public IPlugin, public IMenuProvider {
  Q_OBJECT
  // The id ("abrepeat") and name, read by the host before loading.
  Q_PLUGIN_METADATA(IID IPlugin_iid FILE "abrepeatplugin.json")
  Q_INTERFACES(IPlugin IMenuProvider)

public:
  ABRepeatPlugin();
  ~ABRepeatPlugin() override;

  void initialize(IPluginHost &host) override;
  void shutdown() override;

  // Set A, Set B and Clear. The menu is destroyed before shutdown(), with the
  // menu bar that owns it.
  QMenu *createMenu(QWidget *parent) override;

private:
  IPluginHost *host_ = nullptr;
  // Between initialize() and shutdown().
  std::unique_ptr<ABRepeat> repeat_;
};

#endif // ABREPEATPLUGIN_H

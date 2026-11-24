#ifndef PLAYSTATSPLUGIN_H
#define PLAYSTATSPLUGIN_H

#include "imenuprovider.h"
#include "iplugin.h"
#include "isettingspageprovider.h"
#include "isongfieldprovider.h"
#include <QObject>
#include <memory>

class CloudPlayStatsSyncCoordinator;
class IPluginHost;
class PlayStats;
class PlayStatsStore;

// The play stats plugin's root object (docs/plugins/playstats.md): counts
// plays into its own file, provides play_count and last_played_timestamp as
// song fields, syncs the counts with the cloud, and adds the Play Stats
// Settings page and submenu of Playback.
//
// The root object is one per process while initialize and shutdown run once
// per host, so everything with a lifetime lives between them. initialize does
// no I/O: the file opens on its first use, and sync starts from the event
// loop.
class PlayStatsPlugin : public QObject,
                        public IPlugin,
                        public ISongFieldProvider,
                        public ISettingsPageProvider,
                        public IMenuProvider {
  Q_OBJECT
  // The id ("playstats") and name, read by the host before loading.
  Q_PLUGIN_METADATA(IID IPlugin_iid FILE "playstatsplugin.json")
  Q_INTERFACES(IPlugin ISongFieldProvider ISettingsPageProvider IMenuProvider)

public:
  PlayStatsPlugin();
  ~PlayStatsPlugin() override;

  void initialize(IPluginHost &host) override;
  void shutdown() override;

  QList<SongField> songFields() const override;
  QStringList keyFields() const override;
  QString songKey(const QStringList &keyFieldValues) const override;
  SongFieldValuesByKey values(const QStringList &keys) const override;
  SongFieldValuesByKey allValues() const override;

  QString settingsPageTitle() const override;
  // Applying another UUID rebases the cloud's counts onto it.
  IPluginSettingsPage *createSettingsPage(QWidget *parent) override;

  // Manual Cloud Rebase, enabled while the UUID is valid. The menu is
  // destroyed before shutdown(), with the menu bar that owns it.
  QMenu *createMenu(QWidget *parent) override;

private:
  IPluginHost *host_ = nullptr;
  // Between initialize() and shutdown(); the store outlives the objects
  // that write it.
  std::unique_ptr<PlayStatsStore> store_;
  std::unique_ptr<CloudPlayStatsSyncCoordinator> cloudSync_;
  std::unique_ptr<PlayStats> stats_;
};

#endif // PLAYSTATSPLUGIN_H

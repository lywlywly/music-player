#include "playstatsplugin.h"

#include "cloudplaystatssynccoordinator.h"
#include "iplayerhost.h"
#include "ipluginhost.h"
#include "isongfieldhost.h"
#include "playstats.h"
#include "playstatssettingspage.h"
#include "playstatsstore.h"
#include <QAction>
#include <QDir>
#include <QMenu>
#include <QTimer>

PlayStatsPlugin::PlayStatsPlugin() = default;

PlayStatsPlugin::~PlayStatsPlugin() = default;

void PlayStatsPlugin::initialize(IPluginHost &host) {
  host_ = &host;
  store_ = std::make_unique<PlayStatsStore>(
      QDir(host.dataFolder()).filePath("playstats.db"));
  ISongFieldHost &fieldHost = host.songFieldHost();
  cloudSync_ = std::make_unique<CloudPlayStatsSyncCoordinator>(
      *store_,
      [&fieldHost](const QStringList &keys) { fieldHost.valuesChanged(keys); });
  stats_ = std::make_unique<PlayStats>(
      *store_, fieldHost,
      [this](const QString &key) { cloudSync_->pushIncrement(key); });
  host.player().addListener(stats_.get());
  // Sync starts at the event loop's first turn, after the window shows. The
  // coordinator is the context: a host shut down before that drops it.
  QTimer::singleShot(0, cloudSync_.get(),
                     &CloudPlayStatsSyncCoordinator::startSync);
}

void PlayStatsPlugin::shutdown() {
  host_->player().removeListener(stats_.get());
  stats_.reset();
  cloudSync_.reset();
  store_.reset();
  host_ = nullptr;
}

QList<SongField> PlayStatsPlugin::songFields() const {
  return {
      {.id = "play_count",
       .title = tr("Play Count"),
       .type = SongFieldType::Number,
       .defaultWidth = 120,
       .visibleByDefault = true},
      {.id = "last_played_timestamp",
       .title = tr("Last Played"),
       .type = SongFieldType::DateTime,
       .defaultWidth = 180,
       .visibleByDefault = false},
  };
}

QStringList PlayStatsPlugin::keyFields() const { return playStatsKeyFields(); }

QString PlayStatsPlugin::songKey(const QStringList &keyFieldValues) const {
  return songIdentityKey(keyFieldValues);
}

SongFieldValuesByKey PlayStatsPlugin::values(const QStringList &keys) const {
  return store_->values(keys);
}

SongFieldValuesByKey PlayStatsPlugin::allValues() const {
  return store_->allValues();
}

QString PlayStatsPlugin::settingsPageTitle() const { return tr("Play Stats"); }

IPluginSettingsPage *PlayStatsPlugin::createSettingsPage(QWidget *parent) {
  auto *page = new PlayStatsSettingsPage(parent);
  connect(page, &PlayStatsSettingsPage::uuidChanged, page,
          [this](const QString &uuid) {
            if (!uuid.isEmpty()) {
              cloudSync_->triggerManualRebase();
            }
          });
  return page;
}

QMenu *PlayStatsPlugin::createMenu(QWidget *parent) {
  auto *menu = new QMenu(tr("Play Stats"), parent);
  menu->setObjectName("playStatsMenu");
  QAction *rebase = menu->addAction(tr("Manual Cloud Rebase"));
  rebase->setObjectName("playStatsManualCloudRebase");
  connect(rebase, &QAction::triggered, menu,
          [this]() { cloudSync_->triggerManualRebase(); });
  // Checked each time the menu opens, so the item needs no signal when the
  // UUID changes.
  connect(menu, &QMenu::aboutToShow, menu,
          [this, rebase]() { rebase->setEnabled(cloudSync_->hasValidUuid()); });
  return menu;
}

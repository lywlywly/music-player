#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QObject>
#include <QPluginLoader>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QtConcurrent>
#include <vector>

#include "../pluginapi/iaudiohost.h"
#include "../pluginapi/iplayercontrol.h"
#include "../pluginapi/iplayerhost.h"
#include "../pluginapi/iplugin.h"
#include "../pluginapi/ipluginhost.h"
#include "../pluginapi/isongfieldhost.h"
#include "../pluginapi/isongfieldprovider.h"
#include "../plugins/playstats/cloudsyncsettings.h"
#include "../plugins/playstats/playstats.h"
#include "../plugins/playstats/playstatsstore.h"

namespace {
constexpr const char *kKey = "song|artist|album";

TrackInfo track() {
  TrackInfo info;
  info.fields = {{"title", "Song"}, {"artist", "Artist"}, {"album", "Album"}};
  return info;
}

// Records the keys reported changed.
class RecordingFieldHost : public ISongFieldHost {
public:
  void valuesChanged(const QStringList &keys) override { changed << keys; }
  QList<QStringList> changed;
};

// The host services a plugin gets, with no playback behind them.
class FakePluginHost : public IPluginHost,
                       public IPlayerHost,
                       public IPlayerControl,
                       public IAudioHost {
public:
  explicit FakePluginHost(QString dataFolder)
      : dataFolder_(std::move(dataFolder)) {}

  IPlayerHost &player() override { return *this; }
  IPlayerControl &control() override { return *this; }
  QString dataFolder() const override { return dataFolder_; }
  ISongFieldHost &songFieldHost() override { return fields; }
  IAudioHost &audio() override { return *this; }

  void addListener(IPlayerListener *listener) override {
    listeners.push_back(listener);
  }
  void removeListener(IPlayerListener *listener) override {
    std::erase(listeners, listener);
  }
  void seek(qint64) override {}
  void play() override {}
  void setWanted(bool) override {}
  int read(std::span<float>) const override { return 0; }

  RecordingFieldHost fields;
  std::vector<IPlayerListener *> listeners;

private:
  QString dataFolder_;
};
} // namespace

class TestPlayStats : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void songIdentityKey_normalizesTitleArtistAlbum();
  void store_missingFileReadsNothingAndIsNotCreated();
  void store_writesAndReadsOnAnyThread();
  void counting_startRecordsTheTimeAndReportsTheKey();
  void counting_listenedNearEndCountsOnce();
  void counting_seekToEndWithoutListeningDoesNotCount();
  void counting_pausedTimeDoesNotCount();
  void counting_durationLateCountsAtTheEnd();
  void plugin_initializeDoesNoIoAndShutdownDropsTheQueuedSync();

private:
  QString dbPath() const { return dir_->filePath("data/playstats.db"); }

  QTemporaryDir *dir_ = nullptr;
};

void TestPlayStats::init() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  QSettings().clear();
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
}

void TestPlayStats::cleanup() {
  delete dir_;
  dir_ = nullptr;
  QSettings().clear();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

void TestPlayStats::songIdentityKey_normalizesTitleArtistAlbum() {
  QCOMPARE(songIdentityKey({" Song ", "ARTIST", "Album"}), QString(kKey));
  // A missing field is an empty string.
  QCOMPARE(songIdentityKey({"Intro", "", ""}), QString("intro||"));
  QCOMPARE(playStatsKeyFields(), (QStringList{"title", "artist", "album"}));
}

void TestPlayStats::store_missingFileReadsNothingAndIsNotCreated() {
  PlayStatsStore store(dbPath());
  QVERIFY(store.values({kKey}).isEmpty());
  QVERIFY(store.allValues().isEmpty());
  QVERIFY(store.playCounts().empty());
  // Off the main thread too, as the library load reads it.
  QVERIFY(QtConcurrent::run([&store]() { return store.allValues(); })
              .result()
              .isEmpty());
  QVERIFY(!QFileInfo::exists(dbPath()));
  QVERIFY(!QFileInfo::exists(QFileInfo(dbPath()).absolutePath()));
}

void TestPlayStats::store_writesAndReadsOnAnyThread() {
  PlayStatsStore store(dbPath());
  store.markStarted(kKey, 100);
  QCOMPARE(store.values({kKey}).value(kKey), (SongFieldValues{0, 100}));
  store.countPlay(kKey);
  store.countPlay(kKey);
  // The cloud's counts merge as the larger; a count from the cloud alone has
  // no last played time.
  store.mergeCloudCounts({{kKey, 1}, {"other||", 5}});
  QCOMPARE(store.values({kKey}).value(kKey), (SongFieldValues{2, 100}));
  const SongFieldValues other = store.values({"other||"}).value("other||");
  QCOMPARE(other.at(PlayStatsStore::PlayCount).toInt(), 5);
  QVERIFY(!other.at(PlayStatsStore::LastPlayed).isValid());
  QVERIFY(!store.values({"none||"}).contains("none||"));

  // Another thread's connection sees the main thread's writes.
  const SongFieldValuesByKey all =
      QtConcurrent::run([&store]() { return store.allValues(); }).result();
  QCOMPARE(all.size(), 2);
  QCOMPARE(all.value(kKey), (SongFieldValues{2, 100}));
  const auto counts = store.playCounts();
  QCOMPARE(counts.at(kKey), 2);
  QCOMPARE(counts.at("other||"), 5);

  // Kept in the file: a new store reads it.
  PlayStatsStore reopened(dbPath());
  QCOMPARE(reopened.allValues().size(), 2);
}

void TestPlayStats::counting_startRecordsTheTimeAndReportsTheKey() {
  PlayStatsStore store(dbPath());
  RecordingFieldHost fields;
  QStringList pushed;
  PlayStats stats(store, fields,
                  [&pushed](const QString &key) { pushed << key; });

  stats.trackStarted(track());

  QCOMPARE(fields.changed, QList<QStringList>{{kKey}});
  const SongFieldValues values = store.values({kKey}).value(kKey);
  QCOMPARE(values.at(PlayStatsStore::PlayCount).toInt(), 0);
  QVERIFY(values.at(PlayStatsStore::LastPlayed).toLongLong() > 0);
  QVERIFY(pushed.isEmpty());
}

void TestPlayStats::counting_listenedNearEndCountsOnce() {
  PlayStatsStore store(dbPath());
  RecordingFieldHost fields;
  QStringList pushed;
  PlayStats stats(store, fields,
                  [&pushed](const QString &key) { pushed << key; });
  // The backend reports the duration while the song is set up, before the
  // start.
  stats.durationChanged(120000);
  stats.trackStarted(track());
  for (const qint64 positionMs : {1000, 35000, 70000, 109000, 115000, 119000}) {
    stats.positionChanged(positionMs);
  }
  stats.finished();

  QCOMPARE(pushed, QStringList{kKey});
  QCOMPARE(store.playCounts().at(kKey), 1);
  QCOMPARE(fields.changed.size(), 2);
}

void TestPlayStats::counting_seekToEndWithoutListeningDoesNotCount() {
  PlayStatsStore store(dbPath());
  RecordingFieldHost fields;
  QStringList pushed;
  PlayStats stats(store, fields,
                  [&pushed](const QString &key) { pushed << key; });
  stats.durationChanged(120000);
  stats.trackStarted(track());
  stats.positionChanged(1000);
  stats.seeked(115000);
  stats.positionChanged(115000);
  stats.positionChanged(119000);
  stats.finished();

  QVERIFY(pushed.isEmpty());
  QCOMPARE(store.playCounts().at(kKey), 0);
}

void TestPlayStats::counting_pausedTimeDoesNotCount() {
  PlayStatsStore store(dbPath());
  RecordingFieldHost fields;
  QStringList pushed;
  PlayStats stats(store, fields,
                  [&pushed](const QString &key) { pushed << key; });
  stats.durationChanged(120000);
  stats.trackStarted(track());
  stats.paused();
  for (qint64 positionMs = 1000; positionMs <= 119000; positionMs += 1000) {
    stats.positionChanged(positionMs);
  }
  QVERIFY(pushed.isEmpty());

  // Played again from the start: counted.
  stats.trackStarted(track());
  for (qint64 positionMs = 1000; positionMs <= 119000; positionMs += 1000) {
    stats.positionChanged(positionMs);
  }
  QCOMPARE(pushed, QStringList{kKey});
}

void TestPlayStats::counting_durationLateCountsAtTheEnd() {
  PlayStatsStore store(dbPath());
  RecordingFieldHost fields;
  QStringList pushed;
  PlayStats stats(store, fields,
                  [&pushed](const QString &key) { pushed << key; });
  // The whole song is heard before the backend reports its duration, so only
  // the end of the song can count it.
  stats.durationChanged(0);
  stats.trackStarted(track());
  for (const qint64 positionMs : {1000, 60000, 119000}) {
    stats.positionChanged(positionMs);
  }
  stats.durationChanged(120000);
  QVERIFY(pushed.isEmpty());
  stats.finished();
  QCOMPARE(pushed, QStringList{kKey});
}

void TestPlayStats::plugin_initializeDoesNoIoAndShutdownDropsTheQueuedSync() {
  QPluginLoader loader(QCoreApplication::applicationDirPath() +
                       "/plugins/playstatsplugin");
  QObject *root = loader.instance();
  QVERIFY2(root != nullptr, qPrintable(loader.errorString()));
  auto *plugin = qobject_cast<IPlugin *>(root);
  auto *provider = qobject_cast<ISongFieldProvider *>(root);
  QVERIFY(plugin && provider);
  // A sync that runs clears rebase_pending (the dummy cloud has no rows).
  QSettings settings;
  settings.setValue(CloudSyncSettings::kUserUuid,
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue(CloudSyncSettings::kRebasePending, true);
  const QString dataFolder = dir_->filePath("data");

  // Shut down before the event loop runs: the queued sync never starts.
  {
    FakePluginHost host(dataFolder);
    plugin->initialize(host);
    QCOMPARE(host.listeners.size(), size_t{1});
    QVERIFY(!QFileInfo::exists(dataFolder));
    plugin->shutdown();
    QVERIFY(host.listeners.empty());
  }
  QTest::qWait(50);
  QVERIFY(settings.value(CloudSyncSettings::kRebasePending).toBool());

  // Initialized again on the same root object, as by the next window.
  FakePluginHost host(dataFolder);
  plugin->initialize(host);
  QTRY_VERIFY(!settings.value(CloudSyncSettings::kRebasePending).toBool());
  // The provider reads the file the listener writes.
  host.listeners.front()->trackStarted(track());
  QCOMPARE(provider->allValues().size(), 1);
  QCOMPARE(provider->songKey({"Song", "Artist", "Album"}), QString(kKey));
  QVERIFY(QFileInfo::exists(dataFolder + "/playstats.db"));
  plugin->shutdown();
}

QTEST_MAIN(TestPlayStats)
#include "tst_playstats.moc"

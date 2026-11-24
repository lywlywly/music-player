#include <QObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>
#include <set>
#include <string>

#include "../plugins/playstats/cloudplaystatssynccoordinator.h"
#include "../plugins/playstats/cloudsyncsettings.h"
#include "../plugins/playstats/playstatsstore.h"

namespace {
constexpr const char *kKey = "song|artist|album";

// The store's play_count of kKey, or -1 without a row.
int playCount(const PlayStatsStore &store) {
  const SongFieldValuesByKey values = store.values({kKey});
  return values.contains(kKey)
             ? values.value(kKey).at(PlayStatsStore::PlayCount).toInt()
             : -1;
}

// More keys than one rebase chunk (10) holds: two chunks.
constexpr int kTwoChunksOfKeys = 12;

// One play each of kTwoChunksOfKeys keys.
void countTwoChunksOfKeys(PlayStatsStore &store) {
  for (int i = 0; i < kTwoChunksOfKeys; ++i) {
    store.countPlay(QStringLiteral("song %1|artist|album").arg(i));
  }
}

void setUuid(QSettings &settings) {
  settings.setValue(CloudSyncSettings::kUserUuid,
                    "11111111-1111-1111-1111-111111111111");
}
} // namespace
class TestCloudPlayStatsSyncCoordinator : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void incrementalSync_mergesCloudCount_updatesCursor_andReportsKeys();
  void pushIncrement_pushesWhenUuidIsValid();
  void rebase_cloudHigher_noPush_andClearsPending();
  void rebase_localHigher_pushesDelta_andClearsPending();
  void rebase_manyDeltas_pushesEveryChunk_andClearsPending();
  void rebase_pushFailure_stopsAndKeepsPending();

private:
  QTemporaryDir *dir_ = nullptr;
  PlayStatsStore *store_ = nullptr;
  CloudPlayStatsSyncCoordinator *coordinator_ = nullptr;
  // The keys of each report of changed values.
  QList<QStringList> changedKeys_;
};

void TestCloudPlayStatsSyncCoordinator::init() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();

  const QString org =
      QStringLiteral("music-player-tests-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const QString app =
      QStringLiteral("cloud-sync-coordinator-tests-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  QCoreApplication::setOrganizationName(org);
  QCoreApplication::setApplicationName(app);
  QSettings settings;
  settings.clear();

  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  store_ = new PlayStatsStore(dir_->filePath("playstats.db"));
  changedKeys_.clear();
  coordinator_ = new CloudPlayStatsSyncCoordinator(
      *store_,
      [this](const QStringList &keys) { changedKeys_.push_back(keys); });
}

void TestCloudPlayStatsSyncCoordinator::cleanup() {
  delete coordinator_;
  coordinator_ = nullptr;
  delete store_;
  store_ = nullptr;
  delete dir_;
  dir_ = nullptr;

  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

void TestCloudPlayStatsSyncCoordinator::
    incrementalSync_mergesCloudCount_updatesCursor_andReportsKeys() {
  store_->countPlay(kKey);

  QSettings settings;
  setUuid(settings);
  settings.setValue(CloudSyncSettings::kLastSyncedAt, 1000);
  settings.setValue(CloudSyncSettings::kRebasePending, false);

  CloudPlayStatItem item{
      .songIdentityKey = kKey, .playCount = 7, .updatedAt = 2000};
  CloudPlayStatsSync::setDummyPullPages({{item}}, true, 2000);

  coordinator_->startSync();

  QTRY_COMPARE(changedKeys_.size(), 1);
  QCOMPARE(changedKeys_.front(), QStringList{kKey});
  QCOMPARE(playCount(*store_), 7);
  const qint64 syncedAt =
      settings.value(CloudSyncSettings::kLastSyncedAt, 0).toLongLong();
  QVERIFY(syncedAt >= 2000);
}

void TestCloudPlayStatsSyncCoordinator::pushIncrement_pushesWhenUuidIsValid() {
  // Without a UUID, nothing.
  coordinator_->pushIncrement(kKey);
  QSettings settings;
  setUuid(settings);
  coordinator_->pushIncrement(kKey);

  QTRY_COMPARE(CloudPlayStatsSync::dummyPushCalls().size(), size_t(1));
  const auto calls = CloudPlayStatsSync::dummyPushCalls();
  QCOMPARE(calls[0].songIdentityKey, std::string(kKey));
  QCOMPARE(calls[0].delta, 1);
}

void TestCloudPlayStatsSyncCoordinator::
    rebase_cloudHigher_noPush_andClearsPending() {
  for (int i = 0; i < 3; ++i) {
    store_->countPlay(kKey);
  }

  QSettings settings;
  setUuid(settings);
  settings.setValue(CloudSyncSettings::kRebasePending, true);
  settings.setValue(CloudSyncSettings::kLastSyncedAt, 0);

  CloudPlayStatsSync::setDummyPushResults({});
  CloudPlayStatItem item{
      .songIdentityKey = kKey, .playCount = 10, .updatedAt = 3000};
  CloudPlayStatsSync::setDummyPullPages({{item}}, true, 3000);

  coordinator_->startSync();

  QTRY_VERIFY(!settings.value(CloudSyncSettings::kRebasePending).toBool());
  QCOMPARE(CloudPlayStatsSync::dummyPushCalls().size(), size_t(0));
  QCOMPARE(playCount(*store_), 10);
}

void TestCloudPlayStatsSyncCoordinator::
    rebase_localHigher_pushesDelta_andClearsPending() {
  for (int i = 0; i < 8; ++i) {
    store_->countPlay(kKey);
  }

  QSettings settings;
  setUuid(settings);
  settings.setValue(CloudSyncSettings::kRebasePending, true);
  settings.setValue(CloudSyncSettings::kLastSyncedAt, 0);

  CloudPlayStatsSync::setDummyPushResults({true});
  CloudPlayStatItem item{
      .songIdentityKey = kKey, .playCount = 3, .updatedAt = 3001};
  CloudPlayStatsSync::setDummyPullPages({{item}}, true, 3001);

  coordinator_->startSync();

  QTRY_VERIFY(!settings.value(CloudSyncSettings::kRebasePending).toBool());
  const auto calls = CloudPlayStatsSync::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(1));
  QCOMPARE(calls.front().songIdentityKey, std::string(kKey));
  QCOMPARE(calls.front().delta, 5);
  // The merge keeps the larger count.
  QCOMPARE(playCount(*store_), 8);
}

void TestCloudPlayStatsSyncCoordinator::
    rebase_manyDeltas_pushesEveryChunk_andClearsPending() {
  countTwoChunksOfKeys(*store_);

  QSettings settings;
  setUuid(settings);
  settings.setValue(CloudSyncSettings::kRebasePending, true);
  settings.setValue(CloudSyncSettings::kLastSyncedAt, 0);
  CloudPlayStatsSync::setDummyPushResults({true, true});
  CloudPlayStatsSync::setDummyPullPages({}, true, 0);

  coordinator_->startSync();

  // The second chunk follows the first after the gap.
  QTRY_VERIFY(!settings.value(CloudSyncSettings::kRebasePending).toBool());
  const auto calls = CloudPlayStatsSync::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(kTwoChunksOfKeys));
  std::set<std::string> pushed;
  for (const auto &call : calls) {
    QCOMPARE(call.delta, 1);
    pushed.insert(call.songIdentityKey);
  }
  QCOMPARE(pushed.size(), size_t(kTwoChunksOfKeys));
}

void TestCloudPlayStatsSyncCoordinator::
    rebase_pushFailure_stopsAndKeepsPending() {
  countTwoChunksOfKeys(*store_);

  QSettings settings;
  setUuid(settings);
  settings.setValue(CloudSyncSettings::kRebasePending, true);
  settings.setValue(CloudSyncSettings::kLastSyncedAt, 0);
  CloudPlayStatsSync::setDummyPushResults({false});
  CloudPlayStatsSync::setDummyPullPages({}, true, 0);

  coordinator_->startSync();

  // The first chunk's 10 updates, then nothing: no second chunk after the
  // gap.
  QTRY_COMPARE(CloudPlayStatsSync::dummyPushCalls().size(), size_t(10));
  QTest::qWait(1500);
  QCOMPARE(CloudPlayStatsSync::dummyPushCalls().size(), size_t(10));
  QVERIFY(settings.value(CloudSyncSettings::kRebasePending).toBool());
}
QTEST_MAIN(TestCloudPlayStatsSyncCoordinator)
#include "tst_cloudplaystatssynccoordinator.moc"

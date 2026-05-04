#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

#include "../cloudplaystatssynccoordinator.h"
#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../songlibrary.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &album = QStringLiteral("Album")) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = album.toStdString();
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  song["tracknumber"] = FieldValue("1", ColumnValueType::Number);
  song["date"] = FieldValue("2024-01-01", ColumnValueType::DateTime);
  song["genre"] = "genre";
  song["filepath"] = path.toStdString();
  const std::string identity = util::normalizedText(title).toStdString() + "|" +
                               util::normalizedText(artist).toStdString() +
                               "|" + util::normalizedText(album).toStdString();
  song["song_identity_key"] = identity;
  return song;
}
} // namespace

class TestCloudPlayStatsSyncCoordinator : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void incrementalSync_appliesCloudCount_updatesCursor_andEmitsSongChange();
  void pushIncrementForSongPk_pushesWhenUuidIsValid();
  void rebase_cloudHigher_noPush_andClearsPending();
  void rebase_localHigher_pushesDelta_andClearsPending();
  void rebase_pushFailure_keepsPending();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  CloudPlayStatsSyncService *service_ = nullptr;
  CloudPlayStatsSyncCoordinator *coordinator_ = nullptr;
  QString connectionName_;
};

void TestCloudPlayStatsSyncCoordinator::init() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSyncService::clearDummyPullPages();
  CloudPlayStatsSyncService::clearDummyPushCalls();

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

  connectionName_ =
      QStringLiteral("test_cloudsync_coordinator_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  service_ = new CloudPlayStatsSyncService();
  coordinator_ = new CloudPlayStatsSyncCoordinator(*library_, *service_);
}

void TestCloudPlayStatsSyncCoordinator::cleanup() {
  delete coordinator_;
  coordinator_ = nullptr;
  delete service_;
  service_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;

  CloudPlayStatsSyncService::clearDummyPullPages();
  CloudPlayStatsSyncService::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

void TestCloudPlayStatsSyncCoordinator::
    incrementalSync_appliesCloudCount_updatesCursor_andEmitsSongChange() {
  const int songPk =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/cloud-a.mp3"));

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue("cloud_sync/last_synced_at", 1000);
  settings.setValue("cloud_sync/rebase_pending", false);

  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 7,
                         .updatedAt = 2000};
  CloudPlayStatsSyncService::setDummyPullPages({{item}}, true, 2000);

  QSignalSpy spy(coordinator_, &CloudPlayStatsSyncCoordinator::songDataChanged);
  coordinator_->startSync();

  QTRY_COMPARE(spy.size(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), songPk);
  QCOMPARE(library_->getSongByPK(songPk).at("play_count").text,
           std::string("7"));

  const qint64 syncedAt =
      settings.value("cloud_sync/last_synced_at", 0).toLongLong();
  QVERIFY(syncedAt >= 2000);
}

void TestCloudPlayStatsSyncCoordinator::
    pushIncrementForSongPk_pushesWhenUuidIsValid() {
  const int songPk =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/cloud-b.mp3"));

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");

  CloudPlayStatsSyncService::clearDummyPushCalls();
  coordinator_->pushIncrementForSongPk(songPk);

  const auto calls = CloudPlayStatsSyncService::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(1));
  QCOMPARE(calls[0].songIdentityKey, std::string("song|artist|album"));
  QCOMPARE(calls[0].delta, 1);
}

void TestCloudPlayStatsSyncCoordinator::
    rebase_cloudHigher_noPush_andClearsPending() {
  const int songPk =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/cloud-c.mp3"));
  QVERIFY(library_->markSongPlayedAtStart(songPk, 100));
  QVERIFY(library_->incrementPlayCount(songPk));
  QVERIFY(library_->incrementPlayCount(songPk));
  QVERIFY(library_->incrementPlayCount(songPk)); // local = 3

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue("cloud_sync/rebase_pending", true);
  settings.setValue("cloud_sync/last_synced_at", 0);

  CloudPlayStatsSyncService::clearDummyPushCalls();
  CloudPlayStatsSyncService::setDummyPushResults({});
  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 10,
                         .updatedAt = 3000};
  CloudPlayStatsSyncService::setDummyPullPages({{item}}, true, 3000);

  coordinator_->startSync();

  QTRY_VERIFY(!settings.value("cloud_sync/rebase_pending").toBool());
  QCOMPARE(CloudPlayStatsSyncService::dummyPushCalls().size(), size_t(0));
  QCOMPARE(library_->getSongByPK(songPk).at("play_count").text,
           std::string("10"));
}

void TestCloudPlayStatsSyncCoordinator::
    rebase_localHigher_pushesDelta_andClearsPending() {
  const int songPk =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/cloud-d.mp3"));
  QVERIFY(library_->markSongPlayedAtStart(songPk, 100));
  for (int i = 0; i < 8; ++i) {
    QVERIFY(library_->incrementPlayCount(songPk));
  }

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue("cloud_sync/rebase_pending", true);
  settings.setValue("cloud_sync/last_synced_at", 0);

  CloudPlayStatsSyncService::clearDummyPushCalls();
  CloudPlayStatsSyncService::setDummyPushResults({true});
  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 3,
                         .updatedAt = 3001};
  CloudPlayStatsSyncService::setDummyPullPages({{item}}, true, 3001);

  coordinator_->startSync();

  QTRY_VERIFY(!settings.value("cloud_sync/rebase_pending").toBool());
  const auto calls = CloudPlayStatsSyncService::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(1));
  QCOMPARE(calls.front().songIdentityKey, std::string("song|artist|album"));
  QCOMPARE(calls.front().delta, 5);
  QCOMPARE(library_->getSongByPK(songPk).at("play_count").text,
           std::string("8"));
}

void TestCloudPlayStatsSyncCoordinator::rebase_pushFailure_keepsPending() {
  const int songPk =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/cloud-e.mp3"));
  QVERIFY(library_->markSongPlayedAtStart(songPk, 100));
  for (int i = 0; i < 8; ++i) {
    QVERIFY(library_->incrementPlayCount(songPk));
  }

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue("cloud_sync/rebase_pending", true);
  settings.setValue("cloud_sync/last_synced_at", 0);

  CloudPlayStatsSyncService::clearDummyPushCalls();
  CloudPlayStatsSyncService::setDummyPushResults({false});
  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 3,
                         .updatedAt = 3002};
  CloudPlayStatsSyncService::setDummyPullPages({{item}}, true, 3002);

  coordinator_->startSync();

  QTRY_VERIFY(settings.value("cloud_sync/rebase_pending").toBool());
  const auto calls = CloudPlayStatsSyncService::dummyPushCalls();
  QCOMPARE(calls.size(), size_t(1));
  QCOMPARE(calls.front().delta, 5);
}

QTEST_MAIN(TestCloudPlayStatsSyncCoordinator)
#include "tst_cloudplaystatssynccoordinator.moc"

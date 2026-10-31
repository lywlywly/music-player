#include <QObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../playlistcollection.h"
#include "../songlibrary.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &path) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("discnumber", FieldValue("1", "discnumber"));
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Artist")).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song.insert_or_assign("song_identity_key",
                        FieldValue(identity, "song_identity_key"));
  return song;
}

int queryInt(QSqlDatabase &db, const QString &sql) {
  QSqlQuery q(db);
  if (!q.exec(sql) || !q.next()) {
    return -1;
  }
  return q.value(0).toInt();
}
} // namespace

class TestPlaylistCollection : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void loadAll_createsDefaultPlaylistWithoutSignals();
  void createPlaylist_insertsRowAndAppendsSongs();
  void removePlaylist_rejectsLastAndEmitsInOrder();
  void renamePlaylist_trimsAndRejectsEmpty();
  void notifySongChanged_reachesEveryPlaylist();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  PlaylistCollection *collection_ = nullptr;
  QString connectionName_;
};

void TestPlaylistCollection::init() {
  connectionName_ =
      QStringLiteral("test_playlistcollection_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  collection_ = new PlaylistCollection(*library_, *databaseManager_);
}

void TestPlaylistCollection::cleanup() {
  delete collection_;
  collection_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestPlaylistCollection::loadAll_createsDefaultPlaylistWithoutSignals() {
  QSignalSpy createdSpy(collection_, &PlaylistCollection::playlistCreated);
  collection_->loadAll();

  QCOMPARE(createdSpy.count(), 0);
  QCOMPARE(collection_->playlistIdsInOrder(), std::vector<int>{1});
  QCOMPARE(collection_->name(1), QString("Default"));
  QVERIFY(collection_->playlist(1) != nullptr);
  QCOMPARE(collection_->playlist(1)->getLastPlayed(), 1);
  QVERIFY(collection_->playlist(99) == nullptr);
}

void TestPlaylistCollection::createPlaylist_insertsRowAndAppendsSongs() {
  collection_->loadAll();
  const int s1 = library_->addTolibrary(makeSong("S1", "/tmp/pc-1.mp3"));
  const int s2 = library_->addTolibrary(makeSong("S2", "/tmp/pc-2.mp3"));

  int songCountAtCreated = -1;
  connect(collection_, &PlaylistCollection::playlistCreated, this,
          [&](int playlistId) {
            songCountAtCreated = collection_->playlist(playlistId)->songCount();
          });

  const int playlistId = collection_->createPlaylist("New", {s1, s2});
  QVERIFY(playlistId > 1);
  QCOMPARE(songCountAtCreated, 0);
  const std::vector<int> expectedOrder = {1, playlistId};
  QCOMPARE(collection_->playlistIdsInOrder(), expectedOrder);
  QCOMPARE(collection_->name(playlistId), QString("New"));

  Playlist *playlist = collection_->playlist(playlistId);
  QVERIFY(playlist != nullptr);
  QCOMPARE(playlist->songCount(), 2);
  QCOMPARE(playlist->getPkByIndex(0), s1);
  QCOMPARE(playlist->getPkByIndex(1), s2);
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT tab_order FROM playlists WHERE "
                            "playlist_id=%1")
                        .arg(playlistId)),
           1);
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT COUNT(*) FROM playlist_items WHERE "
                            "playlist_id=%1")
                        .arg(playlistId)),
           2);
}

void TestPlaylistCollection::removePlaylist_rejectsLastAndEmitsInOrder() {
  collection_->loadAll();
  QVERIFY(!collection_->removePlaylist(1));
  QVERIFY(!collection_->removePlaylist(99));

  const int playlistId = collection_->createPlaylist("Second");
  QStringList events;
  connect(collection_, &PlaylistCollection::playlistAboutToBeRemoved, this,
          [&](int id) {
            events << QStringLiteral("about:%1:%2")
                          .arg(id)
                          .arg(collection_->playlist(id) != nullptr);
          });
  connect(collection_, &PlaylistCollection::playlistRemoved, this,
          [&](int id) {
            events << QStringLiteral("removed:%1:%2")
                          .arg(id)
                          .arg(collection_->playlist(id) != nullptr);
          });

  QVERIFY(collection_->removePlaylist(1));
  const QStringList expectedEvents = {"about:1:1", "removed:1:0"};
  QCOMPARE(events, expectedEvents);
  QCOMPARE(collection_->playlistIdsInOrder(), std::vector<int>{playlistId});
  QCOMPARE(queryInt(databaseManager_->db(),
                    "SELECT COUNT(*) FROM playlists WHERE playlist_id=1"),
           0);
  QCOMPARE(queryInt(databaseManager_->db(),
                    QString("SELECT tab_order FROM playlists WHERE "
                            "playlist_id=%1")
                        .arg(playlistId)),
           0);
}

void TestPlaylistCollection::renamePlaylist_trimsAndRejectsEmpty() {
  collection_->loadAll();
  QSignalSpy renamedSpy(collection_, &PlaylistCollection::playlistRenamed);

  QVERIFY(!collection_->renamePlaylist(1, "   "));
  QCOMPARE(renamedSpy.count(), 0);

  QVERIFY(collection_->renamePlaylist(1, "  Renamed  "));
  QCOMPARE(renamedSpy.count(), 1);
  QCOMPARE(renamedSpy.at(0).at(1).toString(), QString("Renamed"));
  QCOMPARE(collection_->name(1), QString("Renamed"));
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT name FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Renamed"));
}

void TestPlaylistCollection::notifySongChanged_reachesEveryPlaylist() {
  collection_->loadAll();
  const int s1 = library_->addTolibrary(makeSong("S1", "/tmp/pc-notify.mp3"));
  collection_->playlist(1)->addSongByPk(s1);
  const int other = collection_->createPlaylist("Other", {s1});
  const int empty = collection_->createPlaylist("Empty");

  QSignalSpy firstSpy(collection_->playlist(1), &Playlist::songDataChanged);
  QSignalSpy otherSpy(collection_->playlist(other), &Playlist::songDataChanged);
  QSignalSpy emptySpy(collection_->playlist(empty), &Playlist::songDataChanged);

  collection_->notifySongChanged(s1);
  QCOMPARE(firstSpy.count(), 1);
  QCOMPARE(otherSpy.count(), 1);
  QCOMPARE(emptySpy.count(), 0);

  collection_->notifySongChanged(-1);
  QCOMPARE(firstSpy.count(), 1);
}

QTEST_MAIN(TestPlaylistCollection)
#include "tst_playlistcollection.moc"

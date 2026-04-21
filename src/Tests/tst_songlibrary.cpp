#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../songlibrary.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &album = {}, const QString &tracknumber = {},
               const QString &date = {}, const QString &genre = {}) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  if (!album.isEmpty()) {
    song["album"] = album.toStdString();
  }
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  if (!tracknumber.isEmpty()) {
    song["tracknumber"] =
        FieldValue(tracknumber.toStdString(), ColumnValueType::Number);
  }
  if (!date.isEmpty()) {
    song["date"] = FieldValue(date.toStdString(), ColumnValueType::DateTime);
  }
  if (!genre.isEmpty()) {
    song["genre"] = genre.toStdString();
  }
  song["filepath"] = path.toStdString();
  return song;
}
} // namespace

class TestSongLibrary : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void addToLibrary_sameFilepathUpdatesExistingSong();
  void loadFromDatabase_loadsBuiltInAndDynamicAttributes();
  void loadFromDatabase_removesUnknownDynamicAttributes();
  void refreshSongFromFile_usesInjectedParserAndSyncsDb();
  void refreshSongsFromFilepaths_dedupesAndReportsProgress();
  void appendAndRemovePlaylistItems();
  void registerQuery_tracksLaterInsertions();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  QString connectionName_;
};

void TestSongLibrary::init() {
  connectionName_ =
      QStringLiteral("test_songlibrary_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
}

void TestSongLibrary::cleanup() {
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestSongLibrary::addToLibrary_sameFilepathUpdatesExistingSong() {
  registry_->addOrUpdateDynamicColumn(
      {"attr:rating", "Rating", ColumnSource::SongAttribute,
       ColumnValueType::Number, true, false, 120});

  MSong first = makeSong("Old Title", "Artist A", "/tmp/song-a.mp3");
  first["attr:rating"] = "3";
  const int firstId = library_->addTolibrary(std::move(first));

  MSong updated = makeSong("New Title", "Artist A", "/tmp/song-a.mp3");
  updated["attr:rating"] = "5";
  const int secondId = library_->addTolibrary(std::move(updated));

  QCOMPARE(secondId, firstId);

  const MSong &song = library_->getSongByPK(firstId);
  QCOMPARE(song.at("title").text, std::string("New Title"));
  QVERIFY(song.contains("attr:rating"));
  QCOMPARE(song.at("attr:rating").text, std::string("5"));

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT COUNT(*) FROM songs"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  q.prepare("SELECT title FROM songs WHERE song_id=:song_id");
  q.bindValue(":song_id", firstId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("New Title"));

  q.prepare("SELECT value_text FROM song_attributes WHERE song_id=:song_id AND "
            "key=:key");
  q.bindValue(":song_id", firstId);
  q.bindValue(":key", "rating");
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("5"));
}

void TestSongLibrary::loadFromDatabase_loadsBuiltInAndDynamicAttributes() {
  registry_->addOrUpdateDynamicColumn(
      {"attr:date_added", "Date Added", ColumnSource::SongAttribute,
       ColumnValueType::DateTime, true, false, 180});

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath) "
             "VALUES (7, 'Song 7', 'Artist 7', 'Album 7', 1, 3, '2022-11-13', "
             "'Jazz', '/tmp/song-7.mp3')"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (7, 'date_added', '2022-11-13 06:45:23+00:00', 'date')"));

  library_->loadFromDatabase();

  const MSong &song = library_->getSongByPK(7);
  QCOMPARE(song.at("title").text, std::string("Song 7"));
  QVERIFY(song.contains("attr:date_added"));
  QCOMPARE(song.at("attr:date_added").text,
           std::string("2022-11-13 06:45:23+00:00"));
  QCOMPARE(song.at("attr:date_added").type, ColumnValueType::DateTime);
}

void TestSongLibrary::loadFromDatabase_removesUnknownDynamicAttributes() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath) "
             "VALUES (8, 'Song 8', 'Artist 8', 'Album 8', 1, 4, '2022-11-14', "
             "'Rock', '/tmp/song-8.mp3')"));
  QVERIFY(q.exec(
      "INSERT INTO song_attributes(song_id, key, value_text, value_type) "
      "VALUES (8, 'orphan_tag', 'stale', 'text')"));

  library_->loadFromDatabase();

  const MSong &song = library_->getSongByPK(8);
  QVERIFY(!song.contains("attr:orphan_tag"));

  QVERIFY(
      q.exec("SELECT COUNT(*) FROM song_attributes WHERE key='orphan_tag'"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestSongLibrary::refreshSongFromFile_usesInjectedParserAndSyncsDb() {
  registry_->addOrUpdateDynamicColumn(
      {"attr:rating", "Rating", ColumnSource::SongAttribute,
       ColumnValueType::Number, true, false, 120});

  int parseCallCount = 0;
  std::string parsedPath;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &) -> MSong {
        ++parseCallCount;
        parsedPath = path;
        MSong parsed =
            makeSong("Refreshed", "Artist B", QString::fromStdString(path),
                     "Album B", "9", "2024-02-03", "Pop");
        parsed["attr:rating"] = FieldValue("8", ColumnValueType::Number);
        return parsed;
      });

  const std::string path = "/tmp/refresh-single.mp3";
  const int songId = localLibrary.addTolibrary(
      makeSong("Old", "Artist A", QString::fromStdString(path)));

  const MSong &beforeRefresh = localLibrary.getSongByPK(songId);
  QCOMPARE(beforeRefresh.at("title").text, std::string("Old"));
  QCOMPARE(beforeRefresh.at("artist").text, std::string("Artist A"));

  const MSong &refreshed = localLibrary.refreshSongFromFile(path);

  QCOMPARE(parseCallCount, 1);
  QCOMPARE(parsedPath, path);
  QCOMPARE(refreshed.at("title").text, std::string("Refreshed"));
  QCOMPARE(refreshed.at("artist").text, std::string("Artist B"));
  QCOMPARE(refreshed.at("album").text, std::string("Album B"));
  QVERIFY(refreshed.contains("attr:rating"));
  QCOMPARE(refreshed.at("attr:rating").text, std::string("8"));

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  q.prepare("SELECT title, artist, album FROM songs WHERE song_id=:song_id");
  q.bindValue(":song_id", songId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("Refreshed"));
  QCOMPARE(q.value(1).toString(), QString("Artist B"));
  QCOMPARE(q.value(2).toString(), QString("Album B"));

  q.prepare("SELECT value_text FROM song_attributes WHERE song_id=:song_id AND "
            "key='rating'");
  q.bindValue(":song_id", songId);
  QVERIFY(q.exec());
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("8"));
}

void TestSongLibrary::refreshSongsFromFilepaths_dedupesAndReportsProgress() {
  int parseCallCount = 0;
  std::vector<std::string> parsedPaths;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &) -> MSong {
        ++parseCallCount;
        parsedPaths.push_back(path);
        return makeSong(QString::fromStdString("Refreshed:" + path), "Artist",
                        QString::fromStdString(path), "Album", "1", "2024",
                        "Genre");
      });

  const std::string pathA = "/tmp/refresh-list-a.mp3";
  const std::string pathB = "/tmp/refresh-list-b.mp3";
  const int idA = localLibrary.addTolibrary(
      makeSong("A", "Artist", QString::fromStdString(pathA)));
  const int idB = localLibrary.addTolibrary(
      makeSong("B", "Artist", QString::fromStdString(pathB)));

  std::vector<std::pair<int, int>> progress;
  localLibrary.refreshSongsFromFilepaths(
      {pathA, pathA, pathB},
      [&](int current, int total) { progress.emplace_back(current, total); });

  QCOMPARE(parseCallCount, 2);
  const std::vector<std::string> expectedPaths = {pathA, pathB};
  QCOMPARE(parsedPaths, expectedPaths);
  const std::vector<std::pair<int, int>> expectedProgress = {{1, 2}, {2, 2}};
  QCOMPARE(progress, expectedProgress);

  QCOMPARE(localLibrary.getSongByPK(idA).at("title").text,
           std::string("Refreshed:/tmp/refresh-list-a.mp3"));
  QCOMPARE(localLibrary.getSongByPK(idB).at("title").text,
           std::string("Refreshed:/tmp/refresh-list-b.mp3"));
}

void TestSongLibrary::appendAndRemovePlaylistItems() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played) VALUES "
                 "(1, 'P1', 1), (2, 'P2', 1)"));

  const int songId = library_->addTolibrary(
      makeSong("Song", "Artist", "/tmp/song-for-playlist.mp3"));
  library_->appendSongToPlaylistInDb(1, songId, 1);
  library_->appendSongToPlaylistInDb(1, songId, 2);
  library_->appendSongToPlaylistInDb(2, songId, 1);

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=2"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  library_->removePlaylistItemsInDb(1);

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=2"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);
}

void TestSongLibrary::registerQuery_tracksLaterInsertions() {
  const std::vector<int> &artistA = library_->registerQuery("ArtistA");
  QCOMPARE(artistA.size(), static_cast<size_t>(0));

  const int id1 =
      library_->addTolibrary(makeSong("S1", "ArtistA", "/tmp/a1.mp3"));
  library_->addTolibrary(makeSong("S2", "ArtistB", "/tmp/b1.mp3"));
  const int id3 =
      library_->addTolibrary(makeSong("S3", "ArtistA", "/tmp/a2.mp3"));

  const std::vector<int> expected = {id1, id3};
  QCOMPARE(artistA, expected);
}

QTEST_MAIN(TestSongLibrary)
#include "tst_songlibrary.moc"

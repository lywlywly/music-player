#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QTime>
#include <QTimeZone>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../songlibrary.h"
#include "../songstore.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber = {}, const QString &date = {},
               const QString &genre = {}) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = "Album";
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
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song["song_identity_key"] = identity;
  return song;
}
} // namespace

class TestSongStore : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void sortByField_numericAndDate();
  void dateFormats_supported();
  void dateFormats_timezoneAndInvalid();
  void sortByField_missingValuesLast();
  void clear_removesPersistedPlaylistItems();
  void loadPlaylistState_readsDbOrder();
  void addSongByPk_persistsPlaylistItems();
  void removeSongByPk_rebuildsIndices();
  void setLastPlayed_persistsPlaylistMetadata();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  QString connectionName_;
};

void TestSongStore::init() {
  connectionName_ =
      QStringLiteral("test_songstore_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
}

void TestSongStore::cleanup() {
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestSongStore::sortByField_numericAndDate() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("A", "Artist", "/tmp/sort-a.mp3", "10", "2023-01-01"));
  store.addSong(makeSong("B", "Artist", "/tmp/sort-b.mp3", "2", "2021"));
  store.addSong(makeSong("C", "Artist", "/tmp/sort-c.mp3", "1", "2022-12-01"));

  store.sortByField("tracknumber", 0);
  QCOMPARE(store.getSongByIndex(0).at("title").text, std::string("C"));
  QCOMPARE(store.getSongByIndex(1).at("title").text, std::string("B"));
  QCOMPARE(store.getSongByIndex(2).at("title").text, std::string("A"));

  store.sortByField("date", 0);
  QCOMPARE(store.getSongByIndex(0).at("title").text, std::string("B"));
  QCOMPARE(store.getSongByIndex(1).at("title").text, std::string("C"));
  QCOMPARE(store.getSongByIndex(2).at("title").text, std::string("A"));
}

void TestSongStore::dateFormats_supported() {
  struct DateCase {
    QString input;
    int year;
    int month;
    int day;
  };

  const std::vector<DateCase> cases = {
      {"2026", 2026, 1, 1},       {"2026.04", 2026, 4, 1},
      {"2026/04", 2026, 4, 1},    {"2026.4.2", 2026, 4, 2},
      {"2026-4-2", 2026, 4, 2},   {"2026-04-02", 2026, 4, 2},
      {"2026.04.02", 2026, 4, 2}, {"2026/04/02", 2026, 4, 2},
      {"2026/4/2", 2026, 4, 2},
  };

  for (const DateCase &c : cases) {
    const int64_t expectedEpochMs =
        QDateTime(QDate(c.year, c.month, c.day), QTime(0, 0), QTimeZone::UTC)
            .toMSecsSinceEpoch();
    const FieldValue parsed(c.input.toStdString(), ColumnValueType::DateTime);
    QCOMPARE(parsed.typed.epochMs, expectedEpochMs);
  }
}

void TestSongStore::dateFormats_timezoneAndInvalid() {
  const FieldValue withOffset("2025-04-23 17:52:00+08:00",
                              ColumnValueType::DateTime);
  const int64_t expectedEpochMs =
      QDateTime(QDate(2025, 4, 23), QTime(9, 52, 0), QTimeZone::UTC)
          .toMSecsSinceEpoch();
  QVERIFY(withOffset.typed.epochMs > 0);
  QCOMPARE(withOffset.typed.epochMs, expectedEpochMs);

  const FieldValue invalid("2026-13-40", ColumnValueType::DateTime);
  QCOMPARE(invalid.typed.epochMs, static_cast<int64_t>(0));
}

void TestSongStore::sortByField_missingValuesLast() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(
      makeSong("Has Rock", "Artist", "/tmp/g1.mp3", "1", "2023", "rock"));
  store.addSong(makeSong("No Genre", "Artist", "/tmp/g2.mp3", "2", "2023"));
  store.addSong(
      makeSong("Has Blues", "Artist", "/tmp/g3.mp3", "3", "2023", "blues"));

  store.sortByField("genre", 0);
  QCOMPARE(store.getSongByIndex(store.songCount() - 1).at("title").text,
           std::string("No Genre"));
}

void TestSongStore::clear_removesPersistedPlaylistItems() {
  SongStore store(*library_, *databaseManager_, 1);
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played, "
                 "tab_order) VALUES (1, 'Default', 1, 0)"));
  int lastPlayed = -1;
  QVERIFY(store.loadPlaylistState(lastPlayed));

  store.addSong(makeSong("S1", "Artist", "/tmp/persist-1.mp3"));
  store.addSong(makeSong("S2", "Artist", "/tmp/persist-2.mp3"));
  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);

  store.clear();
  QCOMPARE(store.songCount(), 0);

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestSongStore::loadPlaylistState_readsDbOrder() {
  const int s1 =
      library_->addTolibrary(makeSong("S1", "Artist", "/tmp/l1.mp3"));
  const int s2 =
      library_->addTolibrary(makeSong("S2", "Artist", "/tmp/l2.mp3"));
  const int s3 =
      library_->addTolibrary(makeSong("S3", "Artist", "/tmp/l3.mp3"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played) VALUES "
                 "(3, 'P3', 1)"));
  QVERIFY(q.exec(QString("INSERT INTO playlist_items(playlist_id, song_id, "
                         "position) VALUES(3, %1, 3)")
                     .arg(s1)));
  QVERIFY(q.exec(QString("INSERT INTO playlist_items(playlist_id, song_id, "
                         "position) VALUES(3, %1, 1)")
                     .arg(s2)));
  QVERIFY(q.exec(QString("INSERT INTO playlist_items(playlist_id, song_id, "
                         "position) VALUES(3, %1, 2)")
                     .arg(s3)));

  SongStore store(*library_, *databaseManager_, 3);
  int lastPlayed = -1;
  QVERIFY(store.loadPlaylistState(lastPlayed));
  QCOMPARE(store.songCount(), 3);
  QCOMPARE(store.getPkByIndex(0), s2);
  QCOMPARE(store.getPkByIndex(1), s3);
  QCOMPARE(store.getPkByIndex(2), s1);
}

void TestSongStore::addSongByPk_persistsPlaylistItems() {
  const int songId =
      library_->addTolibrary(makeSong("S1", "Artist", "/tmp/add-by-pk.mp3"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played, "
                 "tab_order) VALUES (1, 'Default', 1, 0)"));

  SongStore store(*library_, *databaseManager_, 1);
  int lastPlayed = -1;
  QVERIFY(store.loadPlaylistState(lastPlayed));
  QCOMPARE(store.songCount(), 0);

  store.addSongByPk(songId);
  QCOMPARE(store.songCount(), 1);
  QCOMPARE(store.getPkByIndex(0), songId);

  QVERIFY(q.exec(
      "SELECT song_id, position FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), songId);
  QCOMPARE(q.value(1).toInt(), 1);
}

void TestSongStore::removeSongByPk_rebuildsIndices() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/remove-a.mp3"));
  store.addSong(makeSong("S2", "Artist", "/tmp/remove-b.mp3"));
  store.addSong(makeSong("S3", "Artist", "/tmp/remove-c.mp3"));

  const int pk0 = store.getPkByIndex(0);
  const int pk1 = store.getPkByIndex(1);
  const int pk2 = store.getPkByIndex(2);

  store.removeSongByPk(pk1);
  QCOMPARE(store.songCount(), 2);
  QCOMPARE(store.getPkByIndex(0), pk0);
  QCOMPARE(store.getPkByIndex(1), pk2);
  QVERIFY(!store.containsPk(pk1));
  QVERIFY_THROWS_EXCEPTION(std::logic_error, store.getIndexByPk(pk1));
}

void TestSongStore::setLastPlayed_persistsPlaylistMetadata() {
  const int songId =
      library_->addTolibrary(makeSong("S1", "Artist", "/tmp/lastplayed.mp3"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played, "
                 "tab_order) VALUES (1, 'Default', 1, 0)"));

  SongStore store(*library_, *databaseManager_, 1);
  int lastPlayed = -1;
  QVERIFY(store.loadPlaylistState(lastPlayed));
  QCOMPARE(lastPlayed, 1);

  store.setLastPlayed(songId);
  QVERIFY(q.exec("SELECT last_played FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), songId);
}

QTEST_MAIN(TestSongStore)
#include "tst_songstore.moc"

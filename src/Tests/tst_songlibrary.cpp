#include <QObject>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../songlibrary.h"
#include "../utils.h"

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
  const std::string identity = util::normalizedText(title).toStdString() + "|" +
                               util::normalizedText(artist).toStdString() +
                               "|" + util::normalizedText(album).toStdString();
  song["song_identity_key"] = identity;
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
  void loadFromDatabase_loadsPlayStatsDefaultsAndRows();
  void markSongPlayedAtStart_updatesTimestampOnly();
  void incrementPlayCount_updatesMemoryAndDb();
  void playStats_sharedAcrossSameIdentityKey();
  void songIdentityKeyBySongPk_returnsIdentityKey();
  void identityPlayCounts_returnsPerIdentityCounts();
  void applyCloudPlayCount_mergesByMaxAndFansOut();
  void applyCloudPlayCount_handlesUnknownIdentityKey();
  void search_matchesCaseInsensitiveExact();
  void search_supportsAndOr();
  void search_supportsParentheses();
  void search_supportsHasForTagLibStyleMultiValueFields();
  void search_supportsInList();
  void search_supportsInRange();
  void search_supportsQuotedValues();
  void search_supportsNumericFieldComparison();
  void search_supportsRelationalComparisons();
  void search_relationalComparisonSkipsMissingDate();
  void search_supportsNot();
  void search_supportsCustomFields();
  void search_supportsComputedFields();
  void search_noMatchReturnsEmpty();
  void refreshSongFromFile_usesInjectedParserAndSyncsDb();
  void refreshSongFromFile_populatesRemainingFields();
  void refreshSongFromFile_customLyricsFieldsPersist();
  void refreshSongsFromFilepaths_dedupesAndReportsProgress();
  void registerQuery_tracksLaterInsertions();
  void queryHelpers_skipEmptySongSlots();

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
       ColumnValueType::Number, "", true, false, 120});

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
       ColumnValueType::DateTime, "", true, false, 180});

  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec(
      "INSERT INTO song_identities(identity_id, song_identity_key) VALUES "
      "(7, 'song 7|artist 7|album 7')"));
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath, identity_id) "
             "VALUES (7, 'Song 7', 'Artist 7', 'Album 7', 1, 3, '2022-11-13', "
             "'Jazz', '/tmp/song-7.mp3', 7)"));
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
  QVERIFY(q.exec(
      "INSERT INTO song_identities(identity_id, song_identity_key) VALUES "
      "(8, 'song 8|artist 8|album 8')"));
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath, identity_id) "
             "VALUES (8, 'Song 8', 'Artist 8', 'Album 8', 1, 4, '2022-11-14', "
             "'Rock', '/tmp/song-8.mp3', 8)"));
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

void TestSongLibrary::loadFromDatabase_loadsPlayStatsDefaultsAndRows() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec(
      "INSERT INTO song_identities(identity_id, song_identity_key) VALUES "
      "(9, 'song 9|artist 9|album 9'), (10, 'song 10|artist 10|album 10')"));
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath, identity_id) "
             "VALUES (9, 'Song 9', 'Artist 9', 'Album 9', 1, 9, '2022-11-15', "
             "'Rock', '/tmp/song-9.mp3', 9)"));
  QVERIFY(q.exec(
      "INSERT INTO songs(song_id, title, artist, album, discnumber, "
      "tracknumber, date, genre, filepath, identity_id) "
      "VALUES (10, 'Song 10', 'Artist 10', 'Album 10', 1, 10, '2022-11-16', "
      "'Pop', '/tmp/song-10.mp3', 10)"));
  QVERIFY(q.exec("INSERT INTO song_play_stats(identity_id, play_count, "
                 "last_played_timestamp) VALUES (9, 7, 1710000000)"));

  library_->loadFromDatabase();

  const MSong &song9 = library_->getSongByPK(9);
  QVERIFY(song9.contains("play_count"));
  QVERIFY(song9.contains("last_played_timestamp"));
  QCOMPARE(song9.at("play_count").text, std::string("7"));
  QCOMPARE(song9.at("last_played_timestamp").text, std::string("1710000000"));

  const MSong &song10 = library_->getSongByPK(10);
  QVERIFY(!song10.contains("play_count"));
  QVERIFY(!song10.contains("last_played_timestamp"));
}

void TestSongLibrary::markSongPlayedAtStart_updatesTimestampOnly() {
  const int songId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/stats-a.mp3"));

  QVERIFY(library_->markSongPlayedAtStart(songId, 1720000000));
  const MSong &song = library_->getSongByPK(songId);
  QVERIFY(song.contains("play_count"));
  QVERIFY(song.contains("last_played_timestamp"));
  QCOMPARE(song.at("play_count").text, std::string("0"));
  QCOMPARE(song.at("last_played_timestamp").text, std::string("1720000000"));
  const std::string identityIdText = song.at("song_identity_id").text;

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT play_count, last_played_timestamp "
                         "FROM song_play_stats WHERE identity_id=%1")
                     .arg(QString::fromStdString(identityIdText))));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
  QCOMPARE(q.value(1).toLongLong(), 1720000000LL);
}

void TestSongLibrary::incrementPlayCount_updatesMemoryAndDb() {
  const int songId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/stats-b.mp3"));
  QVERIFY(library_->markSongPlayedAtStart(songId, 1721000000));

  QVERIFY(library_->incrementPlayCount(songId));
  QVERIFY(library_->incrementPlayCount(songId));

  const MSong &song = library_->getSongByPK(songId);
  QCOMPARE(song.at("play_count").text, std::string("2"));
  QCOMPARE(song.at("last_played_timestamp").text, std::string("1721000000"));
  const std::string identityIdText = song.at("song_identity_id").text;

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec(QString("SELECT play_count, last_played_timestamp "
                         "FROM song_play_stats WHERE identity_id=%1")
                     .arg(QString::fromStdString(identityIdText))));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);
  QCOMPARE(q.value(1).toLongLong(), 1721000000LL);
}

void TestSongLibrary::playStats_sharedAcrossSameIdentityKey() {
  const int firstId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/stats-c1.mp3"));
  const int secondId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/stats-c2.mp3"));

  QVERIFY(library_->markSongPlayedAtStart(firstId, 1722000000));
  QVERIFY(library_->incrementPlayCount(firstId));
  QVERIFY(library_->incrementPlayCount(secondId));

  library_->loadFromDatabase();

  const MSong &first = library_->getSongByPK(firstId);
  const MSong &second = library_->getSongByPK(secondId);
  QCOMPARE(first.at("play_count").text, std::string("2"));
  QCOMPARE(second.at("play_count").text, std::string("2"));
  QCOMPARE(first.at("last_played_timestamp").text, std::string("1722000000"));
  QCOMPARE(second.at("last_played_timestamp").text, std::string("1722000000"));
}

void TestSongLibrary::songIdentityKeyBySongPk_returnsIdentityKey() {
  const int songId = library_->addTolibrary(
      makeSong("Identity Song", "Artist", "/tmp/id-song.mp3", "Album"));
  const std::string key = library_->songIdentityKeyBySongPk(songId);
  QVERIFY(!key.empty());
  QCOMPARE(key, std::string("identity song|artist|album"));
}

void TestSongLibrary::identityPlayCounts_returnsPerIdentityCounts() {
  const int firstId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/counts-1.mp3"));
  const int secondId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/counts-2.mp3"));
  const int thirdId =
      library_->addTolibrary(makeSong("Other", "Artist", "/tmp/counts-3.mp3"));
  QVERIFY(library_->markSongPlayedAtStart(firstId, 1723000000));
  QVERIFY(library_->incrementPlayCount(firstId));
  QVERIFY(library_->incrementPlayCount(firstId));
  QVERIFY(library_->markSongPlayedAtStart(thirdId, 1723000001));
  QVERIFY(library_->incrementPlayCount(thirdId));

  const auto counts = library_->identityPlayCounts();
  QCOMPARE(counts.at("song|artist|"), 2);
  QCOMPARE(counts.at("other|artist|"), 1);
  Q_UNUSED(secondId);
}

void TestSongLibrary::applyCloudPlayCount_mergesByMaxAndFansOut() {
  const int firstId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/max-1.mp3"));
  const int secondId =
      library_->addTolibrary(makeSong("Song", "Artist", "/tmp/max-2.mp3"));

  QVERIFY(library_->markSongPlayedAtStart(firstId, 1723000000));
  QVERIFY(library_->incrementPlayCount(firstId)); // local count = 1
  const std::string identityKey = library_->songIdentityKeyBySongPk(firstId);

  std::vector<int> affected = library_->applyCloudPlayCount(identityKey, 5, 0);
  QCOMPARE(affected.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(firstId).at("play_count").text,
           std::string("5"));
  QCOMPARE(library_->getSongByPK(secondId).at("play_count").text,
           std::string("5"));

  affected = library_->applyCloudPlayCount(identityKey, 3, 0); // max keeps 5
  QCOMPARE(library_->getSongByPK(firstId).at("play_count").text,
           std::string("5"));
  QCOMPARE(library_->getSongByPK(secondId).at("play_count").text,
           std::string("5"));
}

void TestSongLibrary::applyCloudPlayCount_handlesUnknownIdentityKey() {
  std::vector<int> affected =
      library_->applyCloudPlayCount("ghost|artist|album", 2, 0);
  QVERIFY(affected.empty());
}

void TestSongLibrary::search_matchesCaseInsensitiveExact() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-b.mp3",
                                  "Album", "2", "2024-01-01", "Jazz"));

  ExprParseResult parsed = library_->parseLibraryExpression("genre IS pop");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));
}

void TestSongLibrary::search_supportsAndOr() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-c.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-d.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-e.mp3",
                                  "Album", "3", "2024-01-01", "Jazz"));

  ExprParseResult parsed = library_->parseLibraryExpression(
      "artist IS artist c OR genre IS pop AND title IS song 2");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
  QCOMPARE(library_->getSongByPK(matches[1]).at("title").text,
           std::string("Song 3"));
}

void TestSongLibrary::search_supportsParentheses() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-pa.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-pb.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-pc.mp3",
                                  "Album", "3", "2024-01-01", "Jazz"));

  ExprParseResult parsed = library_->parseLibraryExpression(
      "(artist IS artist c OR genre IS pop) AND title IS song 2");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsHasForTagLibStyleMultiValueFields() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-ha.mp3",
                                  "Album", "1", "2024-01-01",
                                  "Pop / Rock / Jazz"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-hb.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-hc.mp3",
                                  "Album", "3", "2024-01-01", "Classical"));

  ExprParseResult parsed = library_->parseLibraryExpression("genre HAS rock");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));

  ExprParseResult singleValueParsed =
      library_->parseLibraryExpression("genre HAS pop");
  QVERIFY(singleValueParsed.ok());

  const std::vector<int> singleValueMatches =
      library_->search(*singleValueParsed.expr);
  QCOMPARE(singleValueMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(singleValueMatches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(singleValueMatches[1]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsInList() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-ia.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-ib.mp3",
                                  "Album", "2", "2024-01-01", "Rock"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-ic.mp3",
                                  "Album", "3", "2024-01-01", "Classical"));

  ExprParseResult parsed =
      library_->parseLibraryExpression("genre IN [pop, rock]");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(matches[1]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsInRange() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-ir-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-ir-b.mp3",
                                  "Album", "3", "2024-06-01", "Rock"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-ir-c.mp3",
                                  "Album", "7", "2026-01-01", "Jazz"));

  ExprParseResult numberParsed =
      library_->parseLibraryExpression("tracknumber IN [2..5]");
  QVERIFY(numberParsed.ok());
  const std::vector<int> numberMatches = library_->search(*numberParsed.expr);
  QCOMPARE(numberMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(numberMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult dateParsed =
      library_->parseLibraryExpression("date IN [2024-01-01..2024-12-31]");
  QVERIFY(dateParsed.ok());
  const std::vector<int> dateMatches = library_->search(*dateParsed.expr);
  QCOMPARE(dateMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(dateMatches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(dateMatches[1]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsQuotedValues() {
  library_->addTolibrary(makeSong("rock, pop suite", "Artist A",
                                  "/tmp/search-qv-a.mp3", "Album", "1",
                                  "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-qv-b.mp3",
                                  "Album", "2", "2024-01-01", "rock, pop"));

  ExprParseResult scalarParsed =
      library_->parseLibraryExpression("title IS \"rock, pop suite\"");
  QVERIFY(scalarParsed.ok());
  const std::vector<int> scalarMatches = library_->search(*scalarParsed.expr);
  QCOMPARE(scalarMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(scalarMatches[0]).at("title").text,
           std::string("rock, pop suite"));

  ExprParseResult listParsed =
      library_->parseLibraryExpression("genre IN [\"rock, pop\", jazz]");
  QVERIFY(listParsed.ok());
  const std::vector<int> listMatches = library_->search(*listParsed.expr);
  QCOMPARE(listMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(listMatches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsNumericFieldComparison() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-num-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-num-b.mp3",
                                  "Album", "2", "2026", "Rock"));

  ExprParseResult parsed = library_->parseLibraryExpression("tracknumber = 2");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedNumberParsed =
      library_->parseLibraryExpression("tracknumber = 2.0");
  QVERIFY(typedNumberParsed.ok());
  const std::vector<int> typedNumberMatches =
      library_->search(*typedNumberParsed.expr);
  QCOMPARE(typedNumberMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedNumberMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedDateParsed =
      library_->parseLibraryExpression("date IS 2026-01-01");
  QVERIFY(typedDateParsed.ok());
  const std::vector<int> typedDateMatches =
      library_->search(*typedDateParsed.expr);
  QCOMPARE(typedDateMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedDateMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedHasNumberParsed =
      library_->parseLibraryExpression("tracknumber HAS 2.0");
  QVERIFY(typedHasNumberParsed.ok());
  const std::vector<int> typedHasNumberMatches =
      library_->search(*typedHasNumberParsed.expr);
  QCOMPARE(typedHasNumberMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedHasNumberMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult typedHasDateParsed =
      library_->parseLibraryExpression("date HAS 2026-01-01");
  QVERIFY(typedHasDateParsed.ok());
  const std::vector<int> typedHasDateMatches =
      library_->search(*typedHasDateParsed.expr);
  QCOMPARE(typedHasDateMatches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(typedHasDateMatches[0]).at("title").text,
           std::string("Song 2"));

  ExprParseResult invalidParsed =
      library_->parseLibraryExpression("tracknumber = two");
  QVERIFY(!invalidParsed.ok());
}

void TestSongLibrary::search_supportsRelationalComparisons() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-rel-a.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-rel-b.mp3",
                                  "Album", "2", "2024-01-02", "Rock"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-rel-c.mp3",
                                  "Album", "3", "2024-01-03", "Jazz"));

  ExprParseResult ltParsed =
      library_->parseLibraryExpression("tracknumber < 3");
  QVERIFY(ltParsed.ok());
  const std::vector<int> ltMatches = library_->search(*ltParsed.expr);
  QCOMPARE(ltMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(ltMatches[0]).at("title").text,
           std::string("Song 1"));
  QCOMPARE(library_->getSongByPK(ltMatches[1]).at("title").text,
           std::string("Song 2"));

  ExprParseResult gteParsed =
      library_->parseLibraryExpression("tracknumber >= 2");
  QVERIFY(gteParsed.ok());
  const std::vector<int> gteMatches = library_->search(*gteParsed.expr);
  QCOMPARE(gteMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(gteMatches[0]).at("title").text,
           std::string("Song 2"));
  QCOMPARE(library_->getSongByPK(gteMatches[1]).at("title").text,
           std::string("Song 3"));

  ExprParseResult dateParsed =
      library_->parseLibraryExpression("date >= 2024-01-02");
  QVERIFY(dateParsed.ok());
  const std::vector<int> dateMatches = library_->search(*dateParsed.expr);
  QCOMPARE(dateMatches.size(), size_t(2));
  QCOMPARE(library_->getSongByPK(dateMatches[0]).at("title").text,
           std::string("Song 2"));
  QCOMPARE(library_->getSongByPK(dateMatches[1]).at("title").text,
           std::string("Song 3"));

  ExprParseResult invalidParsed =
      library_->parseLibraryExpression("tracknumber < two");
  QVERIFY(!invalidParsed.ok());
}

void TestSongLibrary::search_relationalComparisonSkipsMissingDate() {
  library_->addTolibrary(makeSong(
      "Song 1", "Artist A", "/tmp/search-miss-a.mp3", "Album", "1", "", "Pop"));
  library_->addTolibrary(makeSong("Song 2", "Artist B",
                                  "/tmp/search-miss-b.mp3", "Album", "2",
                                  "1999-01-01", "Rock"));

  ExprParseResult parsed = library_->parseLibraryExpression("date < 2000");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsNot() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-na.mp3",
                                  "Album", "1", "2024-01-01",
                                  "Pop / Rock / Jazz"));
  library_->addTolibrary(makeSong("Song 2", "Artist B", "/tmp/search-nb.mp3",
                                  "Album", "2", "2024-01-01", "Pop"));
  library_->addTolibrary(makeSong("Song 3", "Artist C", "/tmp/search-nc.mp3",
                                  "Album", "3", "2024-01-01", "Classical"));

  ExprParseResult parsed =
      library_->parseLibraryExpression("NOT genre HAS rock AND genre HAS pop");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsCustomFields() {
  registry_->addOrUpdateDynamicColumn(
      {"attr:musicbrainz_trackid", "MusicBrainz Track ID",
       ColumnSource::SongAttribute, ColumnValueType::Text, "", true, false,
       140});

  MSong first = makeSong("Song 1", "Artist A", "/tmp/search-f.mp3");
  first["attr:musicbrainz_trackid"] = "abc123";
  library_->addTolibrary(std::move(first));

  MSong second = makeSong("Song 2", "Artist B", "/tmp/search-g.mp3");
  second["attr:musicbrainz_trackid"] = "def456";
  library_->addTolibrary(std::move(second));

  ExprParseResult parsed =
      library_->parseLibraryExpression("musicbrainz_trackid IS DEF456");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(library_->getSongByPK(matches[0]).at("title").text,
           std::string("Song 2"));
}

void TestSongLibrary::search_supportsComputedFields() {
  QVERIFY(registry_->upsertComputedDefinition(
      databaseManager_->db(),
      {"era", "Era", ColumnSource::Computed, ColumnValueType::Text,
       "IF date < 2000 THEN classic ELSE new", true, true, 140}));
  QVERIFY(registry_->loadDynamicColumns(databaseManager_->db()));

  SongLibrary localLibrary(*registry_, *databaseManager_);
  MSong first = makeSong("Song 1", "Artist A", "/tmp/search-comp-a.mp3",
                         "Album", "1", "1998-01-01", "Pop");
  first["era"] = "classic";
  localLibrary.addTolibrary(std::move(first));
  MSong second = makeSong("Song 2", "Artist B", "/tmp/search-comp-b.mp3",
                          "Album", "2", "2021-01-01", "Rock");
  second["era"] = "new";
  localLibrary.addTolibrary(std::move(second));

  ExprParseResult parsed =
      localLibrary.parseLibraryExpression("era IS classic");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = localLibrary.search(*parsed.expr);
  QCOMPARE(matches.size(), size_t(1));
  QCOMPARE(localLibrary.getSongByPK(matches[0]).at("title").text,
           std::string("Song 1"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT value_text FROM song_computed_attributes WHERE "
                 "key='era' ORDER BY song_id"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("classic"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toString(), QString("new"));
}

void TestSongLibrary::search_noMatchReturnsEmpty() {
  library_->addTolibrary(makeSong("Song 1", "Artist A", "/tmp/search-h.mp3",
                                  "Album", "1", "2024-01-01", "Pop"));

  ExprParseResult parsed =
      library_->parseLibraryExpression("artist IS artist z");
  QVERIFY(parsed.ok());

  const std::vector<int> matches = library_->search(*parsed.expr);
  QVERIFY(matches.empty());
}

void TestSongLibrary::refreshSongFromFile_usesInjectedParserAndSyncsDb() {
  registry_->addOrUpdateDynamicColumn(
      {"attr:rating", "Rating", ColumnSource::SongAttribute,
       ColumnValueType::Number, "", true, false, 120});

  int parseCallCount = 0;
  std::string parsedPath;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *) -> MSong {
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
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *) -> MSong {
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

void TestSongLibrary::refreshSongFromFile_populatesRemainingFields() {
  bool sawNullRemaining = false;
  bool sawProvidedRemaining = false;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *remainingFields)
          -> MSong {
        if (remainingFields) {
          sawProvidedRemaining = true;
          (*remainingFields)["unsynced_lyrics"] = "[00:01.00]embedded";
        } else {
          sawNullRemaining = true;
        }
        return makeSong("Loaded", "Artist", QString::fromStdString(path));
      });

  localLibrary.loadSongFromFile("/tmp/embedded-off.mp3");
  QVERIFY(sawNullRemaining);
  QVERIFY(!sawProvidedRemaining);

  const std::string path = "/tmp/embedded-on.mp3";
  localLibrary.addTolibrary(
      makeSong("Old", "Artist", QString::fromStdString(path)));
  std::unordered_map<std::string, std::string> remainingFields;
  localLibrary.refreshSongFromFile(path, &remainingFields);
  QVERIFY(sawProvidedRemaining);
  QCOMPARE(remainingFields.at("unsynced_lyrics"),
           std::string("[00:01.00]embedded"));
}

void TestSongLibrary::refreshSongFromFile_customLyricsFieldsPersist() {
  registry_->addOrUpdateDynamicColumn(
      {"attr:unsynced_lyrics", "Unsynced Lyrics", ColumnSource::SongAttribute,
       ColumnValueType::Text, "", true, false, 200});
  registry_->addOrUpdateDynamicColumn(
      {"attr:lyrics", "Lyrics", ColumnSource::SongAttribute,
       ColumnValueType::Text, "", true, false, 200});

  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *remainingFields)
          -> MSong {
        if (remainingFields) {
          (*remainingFields)["unsynced_lyrics"] = "[00:00.50]from-tag";
        }
        MSong parsed =
            makeSong("Refreshed", "Artist", QString::fromStdString(path));
        parsed["attr:unsynced_lyrics"] =
            FieldValue("big text", ColumnValueType::Text);
        parsed["attr:lyrics"] =
            FieldValue("fallback text", ColumnValueType::Text);
        return parsed;
      });

  const std::string path = "/tmp/embedded-refresh.mp3";
  localLibrary.addTolibrary(
      makeSong("Old", "Artist", QString::fromStdString(path)));

  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &refreshed =
      localLibrary.refreshSongFromFile(path, &remainingFields);
  QCOMPARE(remainingFields.at("unsynced_lyrics"),
           std::string("[00:00.50]from-tag"));
  QVERIFY(refreshed.contains("attr:unsynced_lyrics"));
  QVERIFY(refreshed.contains("attr:lyrics"));
  QCOMPARE(refreshed.at("attr:unsynced_lyrics").text, std::string("big text"));
  QCOMPARE(refreshed.at("attr:lyrics").text, std::string("fallback text"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT COUNT(*) FROM song_attributes WHERE key IN "
                 "('unsynced_lyrics', 'lyrics')"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);
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

void TestSongLibrary::queryHelpers_skipEmptySongSlots() {
  QSqlDatabase &db = databaseManager_->db();
  QSqlQuery q(db);
  QVERIFY(q.exec(
      "INSERT INTO song_identities(identity_id, song_identity_key) VALUES "
      "(5, 's5|artistx|album'), (9, 's9|artisty|album')"));
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath, identity_id) "
             "VALUES (5, 'S5', 'ArtistX', 'Album', 1, 1, '2024-01-01', 'Pop', "
             "'/tmp/s5.mp3', 5)"));
  QVERIFY(
      q.exec("INSERT INTO songs(song_id, title, artist, album, discnumber, "
             "tracknumber, date, genre, filepath, identity_id) "
             "VALUES (9, 'S9', 'ArtistY', 'Album', 1, 2, '2024-01-01', 'Pop', "
             "'/tmp/s9.mp3', 9)"));

  library_->loadFromDatabase();

  const std::vector<int> matches = library_->query("ArtistX");
  QCOMPARE(matches.size(), static_cast<size_t>(1));
  QCOMPARE(matches[0], 5);

  const std::unordered_set<std::string> artists =
      library_->queryField("artist");
  QCOMPARE(artists.size(), static_cast<size_t>(2));
  QVERIFY(artists.find("ArtistX") != artists.end());
  QVERIFY(artists.find("ArtistY") != artists.end());
}

QTEST_MAIN(TestSongLibrary)
#include "tst_songlibrary.moc"

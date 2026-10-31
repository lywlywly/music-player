#include <QDate>
#include <QDateTime>
#include <QObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTest>
#include <QTime>
#include <QTimeZone>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../songlibrary.h"
#include "../playlist.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber = {}, const QString &date = {},
               const QString &genre = {}) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue(artist.toStdString(), "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("discnumber", FieldValue("1", "discnumber"));
  if (!tracknumber.isEmpty()) {
    song.insert_or_assign("tracknumber",
                          FieldValue(tracknumber.toStdString(), "tracknumber"));
  }
  if (!date.isEmpty()) {
    song.insert_or_assign("date", FieldValue(date.toStdString(), "date"));
  }
  if (!genre.isEmpty()) {
    song.insert_or_assign("genre", FieldValue(genre.toStdString(), "genre"));
  }
  song.insert_or_assign("filepath", FieldValue(path.toStdString(), "filepath"));
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song.insert_or_assign("song_identity_key",
                        FieldValue(identity, "song_identity_key"));
  return song;
}
} // namespace

class TestPlaylist : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void sortByField_numericAndDate();
  void dateFormats_supported();
  void dateFormats_timezoneAndInvalid();
  void sortByField_missingValuesLast();
  void clear_removesPersistedPlaylistItems();
  void loadState_readsDbOrder();
  void addSongByPk_persistsPlaylistItems();
  void removeSong_rebuildsIndices();
  void setLastPlayed_persistsPlaylistMetadata();
  void setLastPlayed_nonPersistentKeepsValueInMemory();
  void refreshMetadataFromFiles_updatesSongsAndReportsProgress();
  void addFiles_insertsBatchWithOneSignalPair();
  void operations_emitBeforeAndAfterSignals();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  QString connectionName_;
};

void TestPlaylist::init() {
  connectionName_ =
      QStringLiteral("test_playlist_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
}

void TestPlaylist::cleanup() {
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestPlaylist::sortByField_numericAndDate() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("A", "Artist", "/tmp/sort-a.mp3", "10", "2023-01-01"));
  playlist.addSong(makeSong("B", "Artist", "/tmp/sort-b.mp3", "2", "2021"));
  playlist.addSong(makeSong("C", "Artist", "/tmp/sort-c.mp3", "1", "2022-12-01"));

  playlist.sortByField("tracknumber", 0);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("C"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text, std::string("B"));
  QCOMPARE(playlist.getSongByIndex(2).at("title").text, std::string("A"));

  playlist.sortByField("date", 0);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("B"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text, std::string("C"));
  QCOMPARE(playlist.getSongByIndex(2).at("title").text, std::string("A"));
}

void TestPlaylist::dateFormats_supported() {
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
    int64_t parsedEpochMs = 0;
    QVERIFY(
        FieldValue::parseDateTimeEpochMs(c.input.toStdString(), parsedEpochMs));
    QCOMPARE(parsedEpochMs, expectedEpochMs);
  }
}

void TestPlaylist::dateFormats_timezoneAndInvalid() {
  int64_t epochSecondsMs = 0;
  QVERIFY(FieldValue::parseDateTimeEpochMs("1747125959", epochSecondsMs));
  QCOMPARE(epochSecondsMs, 1747125959000LL);

  int64_t epochMs = 0;
  QVERIFY(FieldValue::parseDateTimeEpochMs("1747125959000", epochMs));
  QCOMPARE(epochMs, 1747125959000LL);

  int64_t plainDateTimeEpochMs = 0;
  QVERIFY(FieldValue::parseDateTimeEpochMs("2026-05-13 01:45:59",
                                           plainDateTimeEpochMs));
  const int64_t expectedPlainDateTimeEpochMs =
      QDateTime(QDate(2026, 5, 13), QTime(1, 45, 59)).toMSecsSinceEpoch();
  QCOMPARE(plainDateTimeEpochMs, expectedPlainDateTimeEpochMs);

  int64_t withOffsetEpochMs = 0;
  QVERIFY(FieldValue::parseDateTimeEpochMs("2025-04-23 17:52:00+08:00",
                                           withOffsetEpochMs));
  const int64_t expectedEpochMs =
      QDateTime(QDate(2025, 4, 23), QTime(9, 52, 0), QTimeZone::UTC)
          .toMSecsSinceEpoch();
  QVERIFY(withOffsetEpochMs > 0);
  QCOMPARE(withOffsetEpochMs, expectedEpochMs);

  int64_t invalidEpochMs = 0;
  QVERIFY(!FieldValue::parseDateTimeEpochMs("2026-13-40", invalidEpochMs));
}

void TestPlaylist::sortByField_missingValuesLast() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(
      makeSong("Has Rock", "Artist", "/tmp/g1.mp3", "1", "2023", "rock"));
  playlist.addSong(makeSong("No Genre", "Artist", "/tmp/g2.mp3", "2", "2023"));
  playlist.addSong(
      makeSong("Has Blues", "Artist", "/tmp/g3.mp3", "3", "2023", "blues"));

  playlist.sortByField("genre", 0);
  QCOMPARE(playlist.getSongByIndex(playlist.songCount() - 1).at("title").text,
           std::string("No Genre"));
}

void TestPlaylist::clear_removesPersistedPlaylistItems() {
  Playlist playlist(1, *library_, *databaseManager_);
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played, "
                 "tab_order) VALUES (1, 'Default', 1, 0)"));
  QVERIFY(playlist.loadState());

  playlist.addSong(makeSong("S1", "Artist", "/tmp/persist-1.mp3"));
  playlist.addSong(makeSong("S2", "Artist", "/tmp/persist-2.mp3"));
  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 2);

  playlist.clear();
  QCOMPARE(playlist.songCount(), 0);

  QVERIFY(q.exec("SELECT COUNT(*) FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaylist::loadState_readsDbOrder() {
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

  Playlist playlist(3, *library_, *databaseManager_);
  QVERIFY(playlist.loadState());
  QCOMPARE(playlist.songCount(), 3);
  QCOMPARE(playlist.getPkByIndex(0), s2);
  QCOMPARE(playlist.getPkByIndex(1), s3);
  QCOMPARE(playlist.getPkByIndex(2), s1);
}

void TestPlaylist::addSongByPk_persistsPlaylistItems() {
  const int songId =
      library_->addTolibrary(makeSong("S1", "Artist", "/tmp/add-by-pk.mp3"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played, "
                 "tab_order) VALUES (1, 'Default', 1, 0)"));

  Playlist playlist(1, *library_, *databaseManager_);
  QVERIFY(playlist.loadState());
  QCOMPARE(playlist.songCount(), 0);

  playlist.addSongByPk(songId);
  QCOMPARE(playlist.songCount(), 1);
  QCOMPARE(playlist.getPkByIndex(0), songId);

  QVERIFY(q.exec(
      "SELECT song_id, position FROM playlist_items WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), songId);
  QCOMPARE(q.value(1).toInt(), 1);
}

void TestPlaylist::removeSong_rebuildsIndices() {
  Playlist playlist(-1, *library_, *databaseManager_);
  playlist.addSong(makeSong("S1", "Artist", "/tmp/remove-a.mp3"));
  playlist.addSong(makeSong("S2", "Artist", "/tmp/remove-b.mp3"));
  playlist.addSong(makeSong("S3", "Artist", "/tmp/remove-c.mp3"));

  const int pk0 = playlist.getPkByIndex(0);
  const int pk1 = playlist.getPkByIndex(1);
  const int pk2 = playlist.getPkByIndex(2);

  playlist.removeSong(1);
  QCOMPARE(playlist.songCount(), 2);
  QCOMPARE(playlist.getPkByIndex(0), pk0);
  QCOMPARE(playlist.getPkByIndex(1), pk2);
  QVERIFY(!playlist.containsPk(pk1));
  QVERIFY_THROWS_EXCEPTION(std::logic_error, playlist.getIndexByPk(pk1));
}

void TestPlaylist::setLastPlayed_persistsPlaylistMetadata() {
  const int songId =
      library_->addTolibrary(makeSong("S1", "Artist", "/tmp/lastplayed.mp3"));

  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("INSERT INTO playlists(playlist_id, name, last_played, "
                 "tab_order) VALUES (1, 'Default', 1, 0)"));

  Playlist playlist(1, *library_, *databaseManager_);
  QVERIFY(playlist.loadState());
  QCOMPARE(playlist.getLastPlayed(), 1);

  playlist.setLastPlayed(songId);
  QVERIFY(q.exec("SELECT last_played FROM playlists WHERE playlist_id=1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), songId);
}

void TestPlaylist::setLastPlayed_nonPersistentKeepsValueInMemory() {
  Playlist playlist(-1, *library_, *databaseManager_);
  QCOMPARE(playlist.getLastPlayed(), 1);
  playlist.setLastPlayed(-5);
  QCOMPARE(playlist.getLastPlayed(), 1);
  playlist.setLastPlayed(42);
  QCOMPARE(playlist.getLastPlayed(), 42);
}

void TestPlaylist::refreshMetadataFromFiles_updatesSongsAndReportsProgress() {
  int parseCallCount = 0;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &,
          std::unordered_map<std::string, std::string> *) -> MSong {
        ++parseCallCount;
        return makeSong("Refreshed " + QString::fromStdString(path), "Artist",
                        QString::fromStdString(path));
      });
  Playlist playlist(-1, localLibrary, *databaseManager_);
  playlist.addSong(makeSong("Old A", "Artist", "/tmp/refresh-a.mp3"));
  playlist.addSong(makeSong("Old B", "Artist", "/tmp/refresh-b.mp3"));

  QSignalSpy aboutToResetSpy(&playlist, &Playlist::aboutToResetSongs);
  QSignalSpy resetSpy(&playlist, &Playlist::songsReset);
  std::vector<std::pair<int, int>> progress;
  playlist.refreshMetadataFromFiles(
      [&](int current, int total) { progress.emplace_back(current, total); });

  QCOMPARE(parseCallCount, 2);
  const std::vector<std::pair<int, int>> expectedProgress = {{1, 2}, {2, 2}};
  QCOMPARE(progress, expectedProgress);
  QCOMPARE(aboutToResetSpy.count(), 1);
  QCOMPARE(resetSpy.count(), 1);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text,
           std::string("Refreshed /tmp/refresh-a.mp3"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text,
           std::string("Refreshed /tmp/refresh-b.mp3"));
}

void TestPlaylist::addFiles_insertsBatchWithOneSignalPair() {
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [](const std::string &path, const ColumnRegistry &,
         std::unordered_map<std::string, std::string> *) -> MSong {
        return makeSong(QString::fromStdString(path), "Artist",
                        QString::fromStdString(path));
      });
  Playlist playlist(-1, localLibrary, *databaseManager_);
  playlist.addSong(makeSong("Existing", "Artist", "/tmp/add-files-0.mp3"));

  QSignalSpy aboutToInsertSpy(&playlist, &Playlist::aboutToInsertSongs);
  QSignalSpy insertedSpy(&playlist, &Playlist::songsInserted);
  std::vector<std::pair<int, int>> progress;
  int rowsAtAboutToInsert = -1;
  connect(&playlist, &Playlist::aboutToInsertSongs, this,
          [&]() { rowsAtAboutToInsert = playlist.songCount(); });

  const std::vector<int> pks = playlist.addFiles(
      {"/tmp/add-files-1.mp3", "/tmp/add-files-2.mp3"},
      [&](int current, int total) { progress.emplace_back(current, total); });

  QCOMPARE(pks.size(), static_cast<size_t>(2));
  QCOMPARE(playlist.songCount(), 3);
  QCOMPARE(playlist.getPkByIndex(1), pks[0]);
  QCOMPARE(playlist.getPkByIndex(2), pks[1]);
  QCOMPARE(playlist.getSongByIndex(2).at("title").text,
           std::string("/tmp/add-files-2.mp3"));
  const std::vector<std::pair<int, int>> expectedProgress = {{1, 2}, {2, 2}};
  QCOMPARE(progress, expectedProgress);
  QCOMPARE(aboutToInsertSpy.count(), 1);
  QCOMPARE(aboutToInsertSpy.at(0).at(0).toInt(), 1);
  QCOMPARE(aboutToInsertSpy.at(0).at(1).toInt(), 2);
  QCOMPARE(rowsAtAboutToInsert, 1);
  QCOMPARE(insertedSpy.count(), 1);

  QVERIFY(playlist.addFiles({}).empty());
  QCOMPARE(aboutToInsertSpy.count(), 1);
}

void TestPlaylist::operations_emitBeforeAndAfterSignals() {
  Playlist playlist(-1, *library_, *databaseManager_);
  QSignalSpy aboutToInsertSpy(&playlist, &Playlist::aboutToInsertSongs);
  QSignalSpy insertedSpy(&playlist, &Playlist::songsInserted);
  QSignalSpy aboutToRemoveSpy(&playlist, &Playlist::aboutToRemoveSongs);
  QSignalSpy removedSpy(&playlist, &Playlist::songsRemoved);
  QSignalSpy aboutToResetSpy(&playlist, &Playlist::aboutToResetSongs);
  QSignalSpy resetSpy(&playlist, &Playlist::songsReset);
  QSignalSpy reorderedSpy(&playlist, &Playlist::songsReordered);
  QSignalSpy dataChangedSpy(&playlist, &Playlist::songDataChanged);

  playlist.addSong(makeSong("B", "Artist", "/tmp/signals-b.mp3", "2"));
  playlist.addSong(makeSong("A", "Artist", "/tmp/signals-a.mp3", "1"));
  QCOMPARE(aboutToInsertSpy.count(), 2);
  QCOMPARE(insertedSpy.count(), 2);
  QCOMPARE(insertedSpy.at(1).at(0).toInt(), 1);
  QCOMPARE(insertedSpy.at(1).at(1).toInt(), 1);

  playlist.sortByField("tracknumber", 0);
  QCOMPARE(reorderedSpy.count(), 1);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("A"));

  const int pkA = playlist.getPkByIndex(0);
  playlist.emitSongDataChangedBySongPk(pkA);
  QCOMPARE(dataChangedSpy.count(), 1);
  QCOMPARE(dataChangedSpy.at(0).at(0).toInt(), 0);
  playlist.emitSongDataChangedBySongPk(-1);
  playlist.emitSongDataChangedBySongPk(pkA + 1000);
  QCOMPARE(dataChangedSpy.count(), 1);

  playlist.removeSong(0);
  QCOMPARE(aboutToRemoveSpy.count(), 1);
  QCOMPARE(removedSpy.count(), 1);
  QCOMPARE(removedSpy.at(0).at(0).toInt(), 0);

  playlist.clear();
  QCOMPARE(aboutToResetSpy.count(), 1);
  QCOMPARE(resetSpy.count(), 1);
  QCOMPARE(playlist.songCount(), 0);
}

QTEST_MAIN(TestPlaylist)
#include "tst_playlist.moc"

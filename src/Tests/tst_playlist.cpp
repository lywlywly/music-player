#include <QObject>
#include <QSettings>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../playbackqueue.h"
#include "../playlist.h"
#include "../songlibrary.h"
#include "../songstore.h"
#include "../utils.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber = {}) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = "Album";
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  if (!tracknumber.isEmpty()) {
    song["tracknumber"] =
        FieldValue(tracknumber.toStdString(), ColumnValueType::Number);
  }
  song["date"] = FieldValue("2024-01-01", ColumnValueType::DateTime);
  song["genre"] = "genre";
  song["filepath"] = path.toStdString();
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song["song_identity_key"] = identity;
  return song;
}
} // namespace

class TestPlaylist : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void data_returnsSongFieldsAndPlayingStatus();
  void sortByColumnId_statusNoop_tracknumberSorts();
  void emitSongDataChangedBySongPk_emitsOnlyWhenMatched();
  void emitSongDataChangedBySongPk_targetsGivenSong();
  void refreshMetadataFromFiles_updatesSongsAndReportsProgress();
  void columnCount_tracksVisibilityChanges();
  void addSongsAndRemoveSong_updateRowCountAndOrder();
  void setLastPlayed_roundTrips();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layout_ = nullptr;
  PlaybackQueue *queue_ = nullptr;
  QString connectionName_;
};

void TestPlaylist::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("music-player-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_playlist_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  layout_ = new GlobalColumnLayoutManager(*registry_);
  queue_ = new PlaybackQueue();
}

void TestPlaylist::cleanup() {
  delete queue_;
  queue_ = nullptr;
  delete layout_;
  layout_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
}

void TestPlaylist::data_returnsSongFieldsAndPlayingStatus() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("First", "Artist", "/tmp/pl-first.mp3", "1"));
  store.addSong(makeSong("Second", "Artist", "/tmp/pl-second.mp3", "2"));

  Playlist playlist(std::move(store), *queue_, 1, *layout_);
  const QList<QString> visibleIds = layout_->visibleColumnIds();
  const int statusColumn = visibleIds.indexOf("status");
  const int titleColumn = visibleIds.indexOf("title");
  QVERIFY(statusColumn >= 0);
  QVERIFY(titleColumn >= 0);

  queue_->setCurrentId(playlist.getPkByIndex(0), &playlist);
  queue_->setPlaybackStatus(PlaybackQueue::PlaybackStatus::Playing);

  QCOMPARE(playlist.data(playlist.index(0, statusColumn), Qt::DisplayRole)
               .toString(),
           QString::fromUtf8("\u25B6"));
  QCOMPARE(
      playlist.data(playlist.index(0, titleColumn), Qt::DisplayRole).toString(),
      QString("First"));
}

void TestPlaylist::sortByColumnId_statusNoop_tracknumberSorts() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("A", "Artist", "/tmp/pl-a.mp3", "10"));
  store.addSong(makeSong("B", "Artist", "/tmp/pl-b.mp3", "2"));
  store.addSong(makeSong("C", "Artist", "/tmp/pl-c.mp3", "1"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  const std::string before = playlist.getSongByIndex(0).at("title").text;
  playlist.sortByColumnId("status", 0);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, before);

  playlist.sortByColumnId("tracknumber", 0);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("C"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text, std::string("B"));
  QCOMPARE(playlist.getSongByIndex(2).at("title").text, std::string("A"));
}

void TestPlaylist::emitSongDataChangedBySongPk_emitsOnlyWhenMatched() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("A", "Artist", "/tmp/pl-signal-a.mp3", "1"));
  store.addSong(makeSong("B", "Artist", "/tmp/pl-signal-b.mp3", "2"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  QSignalSpy spy(&playlist, &QAbstractItemModel::dataChanged);
  const int songPk = playlist.getPkByIndex(0);

  playlist.emitSongDataChangedBySongPk(songPk);
  QCOMPARE(spy.count(), 1);

  playlist.emitSongDataChangedBySongPk(-1);
  QCOMPARE(spy.count(), 1);
}

void TestPlaylist::emitSongDataChangedBySongPk_targetsGivenSong() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("A1", "Artist", "/tmp/pl-same.mp3", "1"));
  store.addSong(makeSong("B", "Artist", "/tmp/pl-other.mp3", "2"));
  store.addSong(makeSong("A2", "Artist", "/tmp/pl-same.mp3", "3"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  QSignalSpy spy(&playlist, &QAbstractItemModel::dataChanged);
  const int firstPk = playlist.getPkByIndex(0);
  const int thirdPk = playlist.getPkByIndex(2);

  playlist.emitSongDataChangedBySongPk(firstPk);
  QCOMPARE(spy.count(), 1);

  playlist.sortByColumnId("tracknumber", 1);
  spy.clear();
  playlist.emitSongDataChangedBySongPk(thirdPk);
  QCOMPARE(spy.count(), 1);
}

void TestPlaylist::refreshMetadataFromFiles_updatesSongsAndReportsProgress() {
  int parseCallCount = 0;
  SongLibrary localLibrary(
      *registry_, *databaseManager_,
      [&](const std::string &path, const ColumnRegistry &) -> MSong {
        ++parseCallCount;
        if (path == "/tmp/pl-refresh-a.mp3") {
          return makeSong("Refreshed A", "Artist A2",
                          QString::fromStdString(path), "11");
        }
        if (path == "/tmp/pl-refresh-b.mp3") {
          return makeSong("Refreshed B", "Artist B2",
                          QString::fromStdString(path), "22");
        }
        return makeSong("Unexpected", "Artist", QString::fromStdString(path),
                        "1");
      });

  SongStore store(localLibrary, *databaseManager_, -1);
  store.addSong(makeSong("Old A", "Artist A", "/tmp/pl-refresh-a.mp3", "1"));
  store.addSong(makeSong("Old B", "Artist B", "/tmp/pl-refresh-b.mp3", "2"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  QSignalSpy aboutToResetSpy(&playlist,
                             &QAbstractItemModel::modelAboutToBeReset);
  QSignalSpy resetSpy(&playlist, &QAbstractItemModel::modelReset);

  std::vector<std::pair<int, int>> progress;
  playlist.refreshMetadataFromFiles(
      [&](int current, int total) { progress.emplace_back(current, total); });

  QCOMPARE(parseCallCount, 2);
  const std::vector<std::pair<int, int>> expectedProgress = {{1, 2}, {2, 2}};
  QCOMPARE(progress, expectedProgress);
  QCOMPARE(aboutToResetSpy.count(), 1);
  QCOMPARE(resetSpy.count(), 1);

  QCOMPARE(playlist.getSongByIndex(0).at("title").text,
           std::string("Refreshed A"));
  QCOMPARE(playlist.getSongByIndex(0).at("artist").text,
           std::string("Artist A2"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text,
           std::string("Refreshed B"));
  QCOMPARE(playlist.getSongByIndex(1).at("artist").text,
           std::string("Artist B2"));
}

void TestPlaylist::columnCount_tracksVisibilityChanges() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("A", "Artist", "/tmp/pl-cols-a.mp3", "1"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  QVERIFY(!layout_->isColumnVisible("genre"));
  const int before = playlist.columnCount();
  layout_->setColumnVisible("genre", true);
  QCOMPARE(playlist.columnCount(), before + 1);
}

void TestPlaylist::addSongsAndRemoveSong_updateRowCountAndOrder() {
  SongStore store(*library_, *databaseManager_, -1);
  Playlist playlist(std::move(store), *queue_, 1, *layout_);
  QCOMPARE(playlist.songCount(), 0);

  std::vector<MSong> songs;
  songs.push_back(makeSong("A", "Artist", "/tmp/pl-addsongs-a.mp3", "1"));
  songs.push_back(makeSong("B", "Artist", "/tmp/pl-addsongs-b.mp3", "2"));
  playlist.addSongs(std::move(songs));
  QCOMPARE(playlist.songCount(), 2);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("A"));
  QCOMPARE(playlist.getSongByIndex(1).at("title").text, std::string("B"));

  playlist.removeSong(0);
  QCOMPARE(playlist.songCount(), 1);
  QCOMPARE(playlist.getSongByIndex(0).at("title").text, std::string("B"));
}

void TestPlaylist::setLastPlayed_roundTrips() {
  SongStore store(*library_, *databaseManager_, -1);
  Playlist playlist(std::move(store), *queue_, 1, *layout_);
  QCOMPARE(playlist.getLastPlayed(), 1);
  playlist.setLastPlayed(42);
  QCOMPARE(playlist.getLastPlayed(), 42);
}

QTEST_MAIN(TestPlaylist)
#include "tst_playlist.moc"

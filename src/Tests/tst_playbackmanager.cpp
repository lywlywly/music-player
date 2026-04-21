#include <QObject>
#include <QSettings>
#include <QSqlDatabase>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../playbackmanager.h"
#include "../playbackqueue.h"
#include "../playlist.h"

namespace {
MSong makeSong(const QString &title, const QString &artist, const QString &path,
               const QString &tracknumber) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = "Album";
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  song["tracknumber"] =
      FieldValue(tracknumber.toStdString(), ColumnValueType::Number);
  song["date"] = FieldValue("2024-01-01", ColumnValueType::DateTime);
  song["genre"] = "genre";
  song["filepath"] = path.toStdString();
  return song;
}
} // namespace

class TestPlaybackManager : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void playIndex_setsCurrentAndLastPlayed();
  void next_prefersQueuedSongsThenFallsBackToPolicy();
  void prev_wrapsToLastInSequentialPolicy();
  void playPauseStop_updatesPlaybackQueueState();

private:
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  GlobalColumnLayoutManager *layout_ = nullptr;
  PlaybackQueue *queue_ = nullptr;
  PlaybackManager *manager_ = nullptr;
  QString connectionName_;
};

void TestPlaybackManager::init() {
  QCoreApplication::setOrganizationName("music-player-tests");
  QCoreApplication::setApplicationName("playbackmanager-tests");
  QSettings settings;
  settings.clear();

  connectionName_ =
      QStringLiteral("test_playbackmanager_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  layout_ = new GlobalColumnLayoutManager(*registry_);
  queue_ = new PlaybackQueue();
  manager_ = new PlaybackManager(*queue_);
}

void TestPlaybackManager::cleanup() {
  delete manager_;
  manager_ = nullptr;
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

void TestPlaybackManager::playIndex_setsCurrentAndLastPlayed() {
  SongStore store(*library_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-playindex-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-playindex-2.mp3", "2"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(&playlist);
  manager_->setPolicy(Sequential);

  const MSong &song = manager_->playIndex(1);
  QCOMPARE(song.at("title").text, std::string("S2"));

  const auto [currentPk, currentPl] = queue_->getCurrentPk();
  QCOMPARE(currentPk, playlist.getPkByIndex(1));
  QCOMPARE(currentPl, &playlist);
  QCOMPARE(playlist.getLastPlayed(), playlist.getPkByIndex(1));
}

void TestPlaybackManager::next_prefersQueuedSongsThenFallsBackToPolicy() {
  SongStore store(*library_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-next-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-next-2.mp3", "2"));
  store.addSong(makeSong("S3", "Artist", "/tmp/pm-next-3.mp3", "3"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(&playlist);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0);

  manager_->queueEnd(2);
  manager_->queueStart(1);

  const auto &[song1, row1, pl1] = manager_->next();
  QCOMPARE(song1.at("title").text, std::string("S2"));
  QCOMPARE(row1, 1);
  QCOMPARE(pl1, &playlist);

  const auto &[song2, row2, pl2] = manager_->next();
  QCOMPARE(song2.at("title").text, std::string("S3"));
  QCOMPARE(row2, 2);
  QCOMPARE(pl2, &playlist);

  const auto &[song3, row3, pl3] = manager_->next();
  QCOMPARE(song3.at("title").text, std::string("S1"));
  QCOMPARE(row3, 0);
  QCOMPARE(pl3, &playlist);
}

void TestPlaybackManager::prev_wrapsToLastInSequentialPolicy() {
  SongStore store(*library_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-prev-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-prev-2.mp3", "2"));
  store.addSong(makeSong("S3", "Artist", "/tmp/pm-prev-3.mp3", "3"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(&playlist);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0);

  const auto &[song, row, pl] = manager_->prev();
  QCOMPARE(song.at("title").text, std::string("S3"));
  QCOMPARE(row, 2);
  QCOMPARE(pl, &playlist);
}

void TestPlaybackManager::playPauseStop_updatesPlaybackQueueState() {
  SongStore store(*library_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-state-1.mp3", "1"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(&playlist);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0);

  manager_->play();
  QCOMPARE(queue_->getStatus(), PlaybackQueue::PlaybackStatus::Playing);

  manager_->pause();
  QCOMPARE(queue_->getStatus(), PlaybackQueue::PlaybackStatus::Paused);

  manager_->stop();
  QCOMPARE(queue_->getStatus(), PlaybackQueue::PlaybackStatus::None);
  QCOMPARE(queue_->getCurrentPk().first, -1);
  QCOMPARE(queue_->getCurrentPk().second, nullptr);
}

QTEST_MAIN(TestPlaybackManager)
#include "tst_playbackmanager.moc"

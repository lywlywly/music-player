#include <QObject>
#include <QSettings>
#include <QSqlDatabase>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../globalcolumnlayoutmanager.h"
#include "../playbackmanager.h"
#include "../playbackpolicyshuffle.h"
#include "../playbackqueue.h"
#include "../playlist.h"
#include "../utils.h"

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
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song["song_identity_key"] = identity;
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
  void clearPlaylist_inShufflePolicy_doesNotCrash();
  void boundedSetWithHistory_basicInsertNavigationAndResize();
  void sequential_next_wrapsToFirst();
  void shuffle_singleSong_nextPrevAreStable();
  void next_onEmptyPlaylist_returnsInvalid();
  void next_withQueuedSongFromOtherPlaylist_keepsCurrentPolicyProgress();
  void queue_multiplePlaylists_consumesInExpectedOrder();
  void queue_multiplePlaylists_queueStartPreemptsQueueEnd();

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
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-playindex-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-playindex-2.mp3", "2"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
  manager_->setPolicy(Sequential);

  const MSong &song = manager_->playIndex(1);
  QCOMPARE(song.at("title").text, std::string("S2"));

  const auto [currentPk, currentPl] = queue_->getCurrentPk();
  QCOMPARE(currentPk, playlist.getPkByIndex(1));
  QCOMPARE(currentPl, &playlist);
  QCOMPARE(playlist.getLastPlayed(), playlist.getPkByIndex(1));
}

void TestPlaybackManager::next_prefersQueuedSongsThenFallsBackToPolicy() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-next-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-next-2.mp3", "2"));
  store.addSong(makeSong("S3", "Artist", "/tmp/pm-next-3.mp3", "3"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
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
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-prev-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-prev-2.mp3", "2"));
  store.addSong(makeSong("S3", "Artist", "/tmp/pm-prev-3.mp3", "3"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0);

  const auto &[song, row, pl] = manager_->prev();
  QCOMPARE(song.at("title").text, std::string("S3"));
  QCOMPARE(row, 2);
  QCOMPARE(pl, &playlist);
}

void TestPlaybackManager::playPauseStop_updatesPlaybackQueueState() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-state-1.mp3", "1"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
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

void TestPlaybackManager::clearPlaylist_inShufflePolicy_doesNotCrash() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-clear-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-clear-2.mp3", "2"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
  manager_->setPolicy(Shuffle);

  playlist.clear();
  QCOMPARE(playlist.songCount(), 0);
}

void TestPlaybackManager::
    boundedSetWithHistory_basicInsertNavigationAndResize() {
  BoundedSetWithHistory<int> history(2);

  QVERIFY(!history.insert_back(1));
  QVERIFY(!history.insert_back(2));
  QCOMPARE(history.size(), static_cast<size_t>(2));
  QVERIFY(history.contains(1));
  QVERIFY(history.contains(2));

  int *current = history.current();
  QVERIFY(current != nullptr);
  QCOMPARE(*current, 2);

  int *prev = history.prev();
  QVERIFY(prev != nullptr);
  QCOMPARE(*prev, 1);

  int *next = history.next();
  QVERIFY(next != nullptr);
  QCOMPARE(*next, 2);

  history.resize(1);
  QCOMPARE(history.size(), static_cast<size_t>(1));
  QVERIFY(!history.contains(1));
  QVERIFY(history.contains(2));

  history.resize(0);
  QCOMPARE(history.size(), static_cast<size_t>(0));
  QVERIFY(history.current() == nullptr);
  QVERIFY(!history.has_prev());
  QVERIFY(!history.has_next());
}

void TestPlaybackManager::sequential_next_wrapsToFirst() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("S1", "Artist", "/tmp/pm-seq-wrap-1.mp3", "1"));
  store.addSong(makeSong("S2", "Artist", "/tmp/pm-seq-wrap-2.mp3", "2"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
  manager_->setPolicy(Sequential);
  manager_->playIndex(1);

  const auto &[song, row, pl] = manager_->next();
  QCOMPARE(song.at("title").text, std::string("S1"));
  QCOMPARE(row, 0);
  QCOMPARE(pl, &playlist);
}

void TestPlaybackManager::shuffle_singleSong_nextPrevAreStable() {
  SongStore store(*library_, *databaseManager_, -1);
  store.addSong(makeSong("Only", "Artist", "/tmp/pm-shuffle-single.mp3", "1"));
  Playlist playlist(std::move(store), *queue_, 1, *layout_);

  manager_->setView(playlist);
  manager_->setPolicy(Shuffle);

  const auto &[nextSong, nextRow, nextPl] = manager_->next();
  QCOMPARE(nextSong.at("title").text, std::string("Only"));
  QCOMPARE(nextRow, 0);
  QCOMPARE(nextPl, &playlist);

  const auto &[prevSong, prevRow, prevPl] = manager_->prev();
  QCOMPARE(prevSong.at("title").text, std::string("Only"));
  QCOMPARE(prevRow, 0);
  QCOMPARE(prevPl, &playlist);
}

void TestPlaybackManager::next_onEmptyPlaylist_returnsInvalid() {
  SongStore store(*library_, *databaseManager_, -1);
  Playlist playlist(std::move(store), *queue_, -1, *layout_);

  manager_->setView(playlist);
  manager_->setPolicy(Sequential);

  const auto &[song, row, pl] = manager_->next();
  QVERIFY(song.empty());
  QCOMPARE(row, -1);
  QCOMPARE(pl, &playlist);
  QCOMPARE(queue_->getCurrentPk().first, -1);
  QCOMPARE(queue_->getCurrentPk().second, nullptr);
}

void TestPlaybackManager::
    next_withQueuedSongFromOtherPlaylist_keepsCurrentPolicyProgress() {
  SongStore storeA(*library_, *databaseManager_, -1);
  storeA.addSong(makeSong("A1", "Artist", "/tmp/pm-q-other-a1.mp3", "1"));
  storeA.addSong(makeSong("A2", "Artist", "/tmp/pm-q-other-a2.mp3", "2"));
  Playlist playlistA(std::move(storeA), *queue_, 1, *layout_);

  SongStore storeB(*library_, *databaseManager_, -1);
  storeB.addSong(makeSong("B1", "Artist", "/tmp/pm-q-other-b1.mp3", "1"));
  Playlist playlistB(std::move(storeB), *queue_, 1, *layout_);

  manager_->setView(playlistA);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0); // A1 current
  {
    const auto [currentPk, currentPl] = queue_->getCurrentPk();
    QCOMPARE(currentPk, playlistA.getPkByIndex(0));
    QCOMPARE(currentPl, &playlistA);
    QCOMPARE(playlistA.getSongByPk(currentPk).at("title").text,
             std::string("A1"));
  }

  queue_->addLast(playlistB.getPkByIndex(0), &playlistB);

  const auto &[queuedSong, queuedRow, queuedPl] = manager_->next();
  QCOMPARE(queuedSong.at("title").text, std::string("B1"));
  QCOMPARE(queuedRow, 0);
  QCOMPARE(queuedPl, &playlistB);

  const auto &[nextSong, nextRow, nextPl] = manager_->next();
  QCOMPARE(nextSong.at("title").text, std::string("A2"));
  QCOMPARE(nextRow, 1);
  QCOMPARE(nextPl, &playlistA);
}

void TestPlaybackManager::queue_multiplePlaylists_consumesInExpectedOrder() {
  SongStore storeA(*library_, *databaseManager_, -1);
  storeA.addSong(makeSong("A1", "Artist", "/tmp/pm-mp-order-a1.mp3", "1"));
  storeA.addSong(makeSong("A2", "Artist", "/tmp/pm-mp-order-a2.mp3", "2"));
  Playlist playlistA(std::move(storeA), *queue_, 1, *layout_);

  SongStore storeB(*library_, *databaseManager_, -1);
  storeB.addSong(makeSong("B1", "Artist", "/tmp/pm-mp-order-b1.mp3", "1"));
  Playlist playlistB(std::move(storeB), *queue_, 1, *layout_);

  SongStore storeC(*library_, *databaseManager_, -1);
  storeC.addSong(makeSong("C1", "Artist", "/tmp/pm-mp-order-c1.mp3", "1"));
  Playlist playlistC(std::move(storeC), *queue_, 1, *layout_);

  manager_->setView(playlistA);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0); // A1

  queue_->addLast(playlistB.getPkByIndex(0), &playlistB);
  queue_->addLast(playlistC.getPkByIndex(0), &playlistC);

  const auto &[song1, row1, pl1] = manager_->next();
  QCOMPARE(song1.at("title").text, std::string("B1"));
  QCOMPARE(row1, 0);
  QCOMPARE(pl1, &playlistB);

  const auto &[song2, row2, pl2] = manager_->next();
  QCOMPARE(song2.at("title").text, std::string("C1"));
  QCOMPARE(row2, 0);
  QCOMPARE(pl2, &playlistC);

  const auto &[song3, row3, pl3] = manager_->next();
  QCOMPARE(song3.at("title").text, std::string("A2"));
  QCOMPARE(row3, 1);
  QCOMPARE(pl3, &playlistA);
}

void TestPlaybackManager::queue_multiplePlaylists_queueStartPreemptsQueueEnd() {
  SongStore storeA(*library_, *databaseManager_, -1);
  storeA.addSong(makeSong("A1", "Artist", "/tmp/pm-mp-preempt-a1.mp3", "1"));
  storeA.addSong(makeSong("A2", "Artist", "/tmp/pm-mp-preempt-a2.mp3", "2"));
  Playlist playlistA(std::move(storeA), *queue_, 1, *layout_);

  SongStore storeB(*library_, *databaseManager_, -1);
  storeB.addSong(makeSong("B1", "Artist", "/tmp/pm-mp-preempt-b1.mp3", "1"));
  Playlist playlistB(std::move(storeB), *queue_, 1, *layout_);

  SongStore storeC(*library_, *databaseManager_, -1);
  storeC.addSong(makeSong("C1", "Artist", "/tmp/pm-mp-preempt-c1.mp3", "1"));
  Playlist playlistC(std::move(storeC), *queue_, 1, *layout_);

  manager_->setView(playlistA);
  manager_->setPolicy(Sequential);
  manager_->playIndex(0); // A1

  queue_->addLast(playlistB.getPkByIndex(0), &playlistB); // queued later
  queue_->addNext(playlistC.getPkByIndex(0), &playlistC); // should go first

  const auto &[song1, row1, pl1] = manager_->next();
  QCOMPARE(song1.at("title").text, std::string("C1"));
  QCOMPARE(row1, 0);
  QCOMPARE(pl1, &playlistC);

  const auto &[song2, row2, pl2] = manager_->next();
  QCOMPARE(song2.at("title").text, std::string("B1"));
  QCOMPARE(row2, 0);
  QCOMPARE(pl2, &playlistB);
}

QTEST_MAIN(TestPlaybackManager)
#include "tst_playbackmanager.moc"

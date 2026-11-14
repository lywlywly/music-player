#include <QObject>
#include <QSignalSpy>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QTest>
#include <QUuid>

#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../dummyaudioplayer.h"
#include "../monitoredlibrary.h"
#include "../playbackcontroller.h"
#include "../playlistcollection.h"
#include "../songlibrary.h"
#include "../songparser.h"
#include "../spectrum/pcmtap.h"
#include "../utils.h"
#include "testaudiofiles.h"

namespace {
MSong makeSong(const QString &title, const QString &filepath) {
  MSong song;
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue("Artist", "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("filepath",
                        FieldValue(filepath.toStdString(), "filepath"));
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Artist")).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song.insert_or_assign("song_identity_key",
                        FieldValue(identity, "song_identity_key"));
  return song;
}
} // namespace

class TestPlaybackController : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void play_startsLastPlayedSongInSignalOrder();
  void play_afterLastPlayedRowRemoved_startsFirstRow();
  void play_onEmptyOrNoPlaylist_doesNothing();
  void endOfMedia_countsFinishesAndPlaysNext();
  void playStats_seekToEndWithoutListenDoesNotCount();
  void playStats_nearEndWithListenCountsOnce();
  void playStats_notifyEverySongOfTheIdentity();

  void next_overMissingFile_stopsPreviousAndSkips();
  void invalidMedia_skipsToNextSong();
  void playbackFailures_countResetsAfterSongPlays();
  void play_allFilesMissing_stopsAndReportsLastFile();
  void stop_cancelsPendingSkip();

  void next_onEmptyShownPlaylist_stops();
  void next_afterRemovingPlaylistWithQueuedSong_playsShownPlaylist();
  void removingPlayingPlaylist_stops();
  void removingCurrentSongFromLibrary_stopsAndClearsIt();

  void setBackend_stopsAndMovesSignalsAndSettingsToNewPlayer();

private:
  // Appends a song to playlist_; its WAV file is written only if exists.
  void addSong(const QString &name, bool exists = true);
  PlaybackBackendManager *backend() const;
  DummyAudioPlayer *player() const;

  QTemporaryDir *dir_ = nullptr;
  ColumnRegistry *registry_ = nullptr;
  DatabaseManager *databaseManager_ = nullptr;
  SongLibrary *library_ = nullptr;
  MonitoredLibrary *monitored_ = nullptr;
  PlaylistCollection *playlists_ = nullptr;
  PlaybackController *controller_ = nullptr;
  Playlist *playlist_ = nullptr;
  Playlist *shown_ = nullptr;
  QString connectionName_;
};

void TestPlaybackController::init() {
  qputenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER", "1");
  dir_ = new QTemporaryDir();
  QVERIFY(dir_->isValid());
  connectionName_ =
      QStringLiteral("test_playbackcontroller_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  registry_ = new ColumnRegistry();
  databaseManager_ =
      new DatabaseManager(*registry_, ":memory:", connectionName_);
  library_ = new SongLibrary(*registry_, *databaseManager_);
  monitored_ = new MonitoredLibrary(*library_, *registry_, *databaseManager_);
  playlists_ =
      new PlaylistCollection(*library_, *databaseManager_, *monitored_);
  playlists_->loadAll();
  playlist_ = playlists_->playlist(1);
  QVERIFY(playlist_ != nullptr);
  shown_ = playlist_;

  controller_ = new PlaybackController(*library_, *playlists_);
  controller_->setBackend(PlaybackBackendManager::Backend::QMediaPlayer);
  controller_->manager().setView(*playlist_);
  controller_->setShownPlaylist([this]() { return shown_; });
  controller_->setReady(true);
}

void TestPlaybackController::cleanup() {
  delete controller_;
  controller_ = nullptr;
  delete playlists_;
  playlists_ = nullptr;
  delete monitored_;
  monitored_ = nullptr;
  delete library_;
  library_ = nullptr;
  delete databaseManager_;
  databaseManager_ = nullptr;
  delete registry_;
  registry_ = nullptr;
  QSqlDatabase::removeDatabase(connectionName_);
  delete dir_;
  dir_ = nullptr;
  qunsetenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER");
}

void TestPlaybackController::addSong(const QString &name, bool exists) {
  const QString path = dir_->filePath(name + ".wav");
  if (exists) {
    QVERIFY(writeSilentWav(path));
  }
  playlist_->addSong(makeSong(name, path));
}

PlaybackBackendManager *TestPlaybackController::backend() const {
  return controller_->findChild<PlaybackBackendManager *>();
}

DummyAudioPlayer *TestPlaybackController::player() const {
  return qobject_cast<DummyAudioPlayer *>(backend()->player());
}

void TestPlaybackController::play_startsLastPlayedSongInSignalOrder() {
  addSong("Song0");
  addSong("Song1");
  playlist_->setLastPlayed(playlist_->getPkByIndex(1));
  player()->setDurationForTest(120000);

  // The backend reports the duration during setSource, so songAboutToStart
  // comes first and songStarted last.
  QStringList order;
  connect(controller_, &PlaybackController::songAboutToStart, this,
          [&]() { order << "aboutToStart"; });
  connect(controller_, &PlaybackController::durationChanged, this,
          [&]() { order << "duration"; });
  connect(controller_, &PlaybackController::songStarted, this,
          [&]() { order << "started"; });

  controller_->play();

  QCOMPARE(order, (QStringList{"aboutToStart", "duration", "started"}));
  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(1));
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::Playing);
}

void TestPlaybackController::play_afterLastPlayedRowRemoved_startsFirstRow() {
  // Same path as a new playlist, whose last_played = 1 is not in it.
  addSong("Song0");
  addSong("Song1");
  addSong("Song2");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  playlist_->removeSongs({0});

  controller_->play();

  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(0));
}

void TestPlaybackController::play_onEmptyOrNoPlaylist_doesNothing() {
  // Fresh install: the Default playlist is empty and last_played is 1.
  QSignalSpy startedSpy(controller_, &PlaybackController::songStarted);
  controller_->play();
  shown_ = nullptr;
  controller_->play();
  controller_->playRandom();

  QCOMPARE(startedSpy.size(), 0);
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
}

void TestPlaybackController::endOfMedia_countsFinishesAndPlaysNext() {
  addSong("Song0");
  addSong("Song1");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  DummyAudioPlayer *audio = player();
  // The whole song is listened to before the backend reports its duration,
  // so only the end of the song can count the play.
  audio->setDurationForTest(0);
  controller_->play();
  for (const qint64 positionMs : {1000, 60000, 119000}) {
    audio->setPosition(positionMs);
  }
  emit audio->durationChanged(120000);
  QSignalSpy countedSpy(controller_, &PlaybackController::playCounted);
  QSignalSpy finishedSpy(controller_, &PlaybackController::songFinished);
  QSignalSpy movedSpy(controller_, &PlaybackController::movedToRow);

  emit audio->mediaStatusChanged(QMediaPlayer::EndOfMedia);

  QCOMPARE(countedSpy.size(), 1);
  QCOMPARE(countedSpy.at(0).at(0).toInt(), playlist_->getPkByIndex(0));
  QCOMPARE(finishedSpy.size(), 1);
  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(1));
  QCOMPARE(movedSpy.size(), 1);
  QCOMPARE(movedSpy.at(0).at(0).toInt(), 1);
}

void TestPlaybackController::playStats_seekToEndWithoutListenDoesNotCount() {
  addSong("Song");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  player()->setDurationForTest(120000);
  QSignalSpy countedSpy(controller_, &PlaybackController::playCounted);

  controller_->play();
  controller_->seek(115000);

  QCOMPARE(countedSpy.size(), 0);
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT IFNULL(MAX(play_count), 0) FROM song_play_stats"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestPlaybackController::playStats_nearEndWithListenCountsOnce() {
  addSong("Song");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  player()->setDurationForTest(120000);
  QSignalSpy countedSpy(controller_, &PlaybackController::playCounted);

  controller_->play();
  for (const qint64 positionMs : {1000, 35000, 70000, 109000, 115000, 119000}) {
    player()->setPosition(positionMs);
  }

  QCOMPARE(countedSpy.size(), 1);
  QCOMPARE(countedSpy.at(0).at(0).toInt(), playlist_->getPkByIndex(0));
  QSqlQuery q(databaseManager_->db());
  QVERIFY(q.exec("SELECT play_count, last_played_timestamp "
                 "FROM song_play_stats"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);
  QVERIFY(q.value(1).toLongLong() > 0);
  QVERIFY(!q.next());
}

void TestPlaybackController::playStats_notifyEverySongOfTheIdentity() {
  // Two files with the same title, artist and album: one identity, so shared
  // play stats. Tagged, as playing re-reads the tags.
  for (const QString &name :
       {QStringLiteral("a.wav"), QStringLiteral("b.wav")}) {
    const QString path = dir_->filePath(name);
    QVERIFY(writeSilentWav(path));
    QVERIFY(SongParser::writeTags(
        path.toStdString(),
        {{"title", "Song"}, {"artist", "Artist"}, {"album", "Album"}},
        *registry_));
    playlist_->addSong(makeSong("Song", path));
  }
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  player()->setDurationForTest(120000);
  QSignalSpy changedSpy(playlist_, &Playlist::songDataChanged);

  controller_->play();
  QCOMPARE(changedSpy.count(), 2);
  for (const qint64 positionMs : {1000, 35000, 70000, 109000, 115000, 119000}) {
    player()->setPosition(positionMs);
  }

  QCOMPARE(changedSpy.count(), 4);
  QCOMPARE(changedSpy.at(2).at(0).toInt() + changedSpy.at(3).at(0).toInt(), 1);
}

void TestPlaybackController::next_overMissingFile_stopsPreviousAndSkips() {
  // Skipping a missing file behaves like pressing Next again: the previous
  // song stops at once, then the following song plays, and the last
  // movedToRow is that song's row, not the skipped one's.
  addSong("Song0");
  addSong("Missing1", false);
  addSong("Song2");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  controller_->play();

  QSignalSpy backendStatusSpy(player(), &AudioPlayer::mediaStatusChanged);
  QSignalSpy movedSpy(controller_, &PlaybackController::movedToRow);
  controller_->next();
  // The missing file is detected synchronously; song 0 is stopped before the
  // skip runs.
  QVERIFY(!backendStatusSpy.isEmpty());
  QCOMPARE(backendStatusSpy.at(0).at(0).value<QMediaPlayer::MediaStatus>(),
           QMediaPlayer::NoMedia);
  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(0));

  QTRY_COMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(2));
  QCOMPARE(movedSpy.size(), 2);
  QCOMPARE(movedSpy.at(1).at(0).toInt(), 2);
}

void TestPlaybackController::invalidMedia_skipsToNextSong() {
  // The file exists but the backend cannot decode it.
  addSong("Song0");
  addSong("Song1");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  controller_->play();
  DummyAudioPlayer *audio = player();

  emit audio->mediaStatusChanged(QMediaPlayer::InvalidMedia);

  QTRY_COMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(1));
}

void TestPlaybackController::playbackFailures_countResetsAfterSongPlays() {
  // Failures are counted while consecutive: once a song plays, a later
  // missing file is skipped again instead of reaching the stop limit.
  // Two songs, so two consecutive failures would stop playback.
  addSong("Missing0", false);
  addSong("Song1");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  QSignalSpy failedSpy(controller_, &PlaybackController::playbackFailed);

  controller_->play(); // row 0 fails, row 1 plays
  QTRY_COMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(1));
  player()->setPosition(500); // row 1 is playing

  QSignalSpy startedSpy(controller_, &PlaybackController::songStarted);
  controller_->next(); // wraps to row 0, which fails again
  QTRY_COMPARE(startedSpy.size(), 1);
  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(1));
  QCOMPARE(failedSpy.size(), 0);
}

void TestPlaybackController::play_allFilesMissing_stopsAndReportsLastFile() {
  addSong("Missing1", false);
  addSong("Missing2", false);
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  QSignalSpy failedSpy(controller_, &PlaybackController::playbackFailed);

  controller_->play();

  // Every song was tried once, then playback stopped instead of looping.
  QTRY_COMPARE(failedSpy.size(), 1);
  QVERIFY(failedSpy.at(0).at(0).toString().endsWith("Missing2.wav"));
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
}

void TestPlaybackController::stop_cancelsPendingSkip() {
  addSong("Missing0", false);
  addSong("Song1");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  QSignalSpy startedSpy(controller_, &PlaybackController::songStarted);

  controller_->play(); // row 0 fails; the skip to row 1 is pending
  controller_->stop();
  QTest::qWait(50);

  QCOMPARE(startedSpy.size(), 0);
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
}

void TestPlaybackController::next_onEmptyShownPlaylist_stops() {
  // Playing from playlist A while an empty playlist B is shown: Next has
  // nothing to play, so playback stops instead of staying "Playing" with no
  // song.
  addSong("Song");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  controller_->play();
  Playlist *empty = playlists_->playlist(playlists_->createPlaylist("B"));
  controller_->manager().setView(*empty);
  shown_ = empty;
  QSignalSpy stateSpy(controller_, &PlaybackController::playbackStateChanged);

  controller_->next();

  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
  QCOMPARE(stateSpy.size(), 1);
  QCOMPARE(stateSpy.at(0).at(0).value<PlaybackQueue::PlaybackStatus>(),
           PlaybackQueue::PlaybackStatus::None);
}

void TestPlaybackController::
    next_afterRemovingPlaylistWithQueuedSong_playsShownPlaylist() {
  // Queue a song from playlist B, remove B, then Next: the queue must not
  // keep the destroyed playlist.
  addSong("SongA");
  addSong("SongB");
  const int idB = playlists_->createPlaylist("B", {playlist_->getPkByIndex(1)});
  controller_->manager().setView(*playlists_->playlist(idB));
  controller_->manager().queueEnd(0);
  controller_->manager().setView(*playlist_);

  QVERIFY(playlists_->removePlaylist(idB));
  QVERIFY(controller_->queue().empty());
  controller_->next();

  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(0));
}

void TestPlaybackController::removingPlayingPlaylist_stops() {
  addSong("Song");
  const int idB = playlists_->createPlaylist("B", {playlist_->getPkByIndex(0)});
  Playlist *playlistB = playlists_->playlist(idB);
  controller_->manager().setView(*playlistB);
  shown_ = playlistB;
  controller_->play();
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::Playing);

  controller_->manager().setView(*playlist_);
  QVERIFY(playlists_->removePlaylist(idB));
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
}

void TestPlaybackController::removingCurrentSongFromLibrary_stopsAndClearsIt() {
  addSong("Song0");
  addSong("Song1");
  addSong("Song2");
  QVERIFY(controller_->currentSong() == nullptr);
  controller_->play();
  const int currentPk = playlist_->getPkByIndex(0);
  QCOMPARE(controller_->currentSongPk(), currentPk);
  QCOMPARE(controller_->currentSong(), &library_->getSongByPK(currentPk));

  // In no other playlist, so each removal also removes it from the library.
  playlist_->removeSongs({1});
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::Playing);
  QCOMPARE(controller_->currentSongPk(), currentPk);

  playlist_->removeSongs({0});
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
  QCOMPARE(controller_->currentSongPk(), -1);
  QVERIFY(controller_->currentSong() == nullptr);

  // Stopped: the song is kept as current until it is removed.
  controller_->play();
  controller_->stop();
  QCOMPARE(controller_->currentSongPk(), playlist_->getPkByIndex(0));
  playlist_->removeSongs({0});
  QCOMPARE(controller_->currentSongPk(), -1);
}

void TestPlaybackController::
    setBackend_stopsAndMovesSignalsAndSettingsToNewPlayer() {
  addSong("Song");
  playlist_->setLastPlayed(playlist_->getPkByIndex(0));
  controller_->play();
  controller_->setVolume(40);
  const auto tap = std::make_shared<PcmTap>();
  controller_->setPcmTap(tap);
  QCOMPARE(player()->pcmTap(), tap);
  controller_->setReplayGainMode(ReplayGainMode::Album);
  QCOMPARE(player()->replayGainMode(), ReplayGainMode::Album);
  QSignalSpy stateSpy(controller_, &PlaybackController::playbackStateChanged);

  controller_->setBackend(PlaybackBackendManager::Backend::GStreamer);
  QCOMPARE(controller_->status(), PlaybackQueue::PlaybackStatus::None);
  QCOMPARE(stateSpy.size(), 1);
  QCOMPARE(stateSpy.at(0).at(0).value<PlaybackQueue::PlaybackStatus>(),
           PlaybackQueue::PlaybackStatus::None);
  QCOMPARE(backend()->currentBackend(),
           PlaybackBackendManager::Backend::GStreamer);
  DummyAudioPlayer *newPlayer = player();
  QVERIFY(newPlayer != nullptr);

  QCOMPARE(newPlayer->volumeForTest(), 40);
  QCOMPARE(newPlayer->pcmTap(), tap);
  QCOMPARE(newPlayer->replayGainMode(), ReplayGainMode::Album);
  // Created after the switch: stopping the old player reports position 0.
  QSignalSpy positionSpy(controller_, &PlaybackController::positionChanged);
  newPlayer->setPosition(2500);
  QCOMPARE(positionSpy.size(), 1);
  QCOMPARE(positionSpy.at(0).at(0).toLongLong(), 2500LL);
}

QTEST_MAIN(TestPlaybackController)
#include "tst_playbackcontroller.moc"

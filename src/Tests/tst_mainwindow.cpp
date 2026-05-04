#include <QAction>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLineEdit>
#include <QMainWindow>
#include <QObject>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>

#include "../cloudplaystatssyncservice.h"
#include "../dummyaudioplayer.h"
#include "../dummysystemmediainterface.h"
#include "../librarysearchdialog.h"
#include "../mainwindow.h"
#include "../playbackbackendmanager.h"
#include "../playlisttabs.h"
#include "../utils.h"

namespace {
bool writeSilentWav(const QString &path) {
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly)) {
    return false;
  }

  constexpr quint32 sampleRate = 8000;
  constexpr quint16 channels = 1;
  constexpr quint16 bitsPerSample = 8;
  const QByteArray pcm(800, static_cast<char>(0x80));
  const quint32 dataSize = static_cast<quint32>(pcm.size());
  const quint32 byteRate = sampleRate * channels * (bitsPerSample / 8);
  const quint16 blockAlign = channels * (bitsPerSample / 8);
  const quint32 chunkSize = 36 + dataSize;

  QDataStream out(&file);
  out.setByteOrder(QDataStream::LittleEndian);

  file.write("RIFF", 4);
  out << chunkSize;
  file.write("WAVE", 4);
  file.write("fmt ", 4);
  out << quint32(16);
  out << quint16(1);
  out << channels;
  out << sampleRate;
  out << byteRate;
  out << blockAlign;
  out << bitsPerSample;
  file.write("data", 4);
  out << dataSize;
  file.write(pcm);

  return out.status() == QDataStream::Ok;
}

MSong makeSong(const QString &title, const QString &artist,
               const QString &filepath) {
  MSong song;
  song["title"] = title.toStdString();
  song["artist"] = artist.toStdString();
  song["album"] = "Album";
  song["discnumber"] = FieldValue("1", ColumnValueType::Number);
  song["tracknumber"] = FieldValue("1", ColumnValueType::Number);
  song["date"] = FieldValue("2024-01-01", ColumnValueType::DateTime);
  song["genre"] = "genre";
  song["filepath"] = filepath.toStdString();
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song["song_identity_key"] = identity;
  return song;
}

int statusColumn(const Playlist *playlist) {
  for (int col = 0; col < playlist->columnCount(); ++col) {
    if (playlist->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString() ==
        "Status") {
      return col;
    }
  }
  return -1;
}

QString statusAt(const Playlist *playlist, int row, int statusCol) {
  return playlist->data(playlist->index(row, statusCol), Qt::DisplayRole)
      .toString();
}
} // namespace

class TestMainWindow : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void menuPlaybackActions_areWired();
  void playbackButtons_areWired();
  void clearPlaylistAction_clearsCurrentPlaylist();
  void systemMediaRequests_areWired();
  void backendSignals_updateUiAndSystemMedia();
  void systemMediaToggleRequest_togglesPlayback();
  void sliderReleased_invokesSeekFlow();
  void playlistTableBackspace_removesSelectedRow();
  void queueActions_fromContextMenu_areWired();
  void playbackOrderMenuActions_areExclusive();
  void librarySearchAction_opensDialog();
  void librarySearchDialog_canCreateNewPlaylistTabFromResults();
  void playStats_seekToEndWithoutListenDoesNotCount();
  void playStats_nearEndWithListenCountsOnceAndRefreshes();
  void cloudSync_startupPull_appliesCloudPlayCount_integration();

private:
  MainWindow *window_ = nullptr;
  QTemporaryDir *workDir_ = nullptr;
  QString oldCwd_;
};

void TestMainWindow::init() {
  qputenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER", "1");
  qputenv("MYPLAYER_USE_DUMMY_MEDIA_INTERFACE", "1");
  const QString org =
      QStringLiteral("music-player-tests-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const QString app =
      QStringLiteral("mainwindow-tests-%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  QCoreApplication::setOrganizationName(org);
  QCoreApplication::setApplicationName(app);

  workDir_ = new QTemporaryDir();
  QVERIFY(workDir_->isValid());
  oldCwd_ = QDir::currentPath();
  QVERIFY(QDir::setCurrent(workDir_->path()));

  window_ = new MainWindow();
}

void TestMainWindow::cleanup() {
  delete window_;
  window_ = nullptr;
  QTest::qWait(100);
  QDir::setCurrent(oldCwd_);
  delete workDir_;
  workDir_ = nullptr;
  qunsetenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER");
  qunsetenv("MYPLAYER_USE_DUMMY_MEDIA_INTERFACE");
}

void TestMainWindow::menuPlaybackActions_areWired() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString wav1 = workDir_->filePath("song1.wav");
  const QString wav2 = workDir_->filePath("song2.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));

  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *pauseAction = window_->findChild<QAction *>("actionPause");
  QAction *stopAction = window_->findChild<QAction *>("actionStop");
  QAction *nextAction = window_->findChild<QAction *>("actionNext");
  QAction *prevAction = window_->findChild<QAction *>("actionPrevious");
  QVERIFY(playAction != nullptr);
  QVERIFY(pauseAction != nullptr);
  QVERIFY(stopAction != nullptr);
  QVERIFY(nextAction != nullptr);
  QVERIFY(prevAction != nullptr);

  playAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));

  pauseAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u23F8"));

  playAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));

  prevAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));

  stopAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString{});
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString{});
}

void TestMainWindow::playbackButtons_areWired() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString wav = workDir_->filePath("button-song.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QPushButton *playButton = window_->findChild<QPushButton *>("play_button");
  QPushButton *pauseButton = window_->findChild<QPushButton *>("pause_button");
  QPushButton *stopButton = window_->findChild<QPushButton *>("stop_button");
  QVERIFY(playButton != nullptr);
  QVERIFY(pauseButton != nullptr);
  QVERIFY(stopButton != nullptr);

  QTest::mouseClick(playButton, Qt::LeftButton);
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));

  QTest::mouseClick(pauseButton, Qt::LeftButton);
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u23F8"));

  QTest::mouseClick(stopButton, Qt::LeftButton);
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString{});
}

void TestMainWindow::clearPlaylistAction_clearsCurrentPlaylist() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString wav = workDir_->filePath("clear.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  QCOMPARE(playlist->songCount(), 1);

  QAction *clearAction = window_->findChild<QAction *>("actionClear_Playlist");
  QVERIFY(clearAction != nullptr);
  clearAction->trigger();
  QCOMPARE(playlist->songCount(), 0);
}

void TestMainWindow::systemMediaRequests_areWired() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  DummySystemMediaInterface *media =
      window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);

  const QString wav1 = workDir_->filePath("media-song1.wav");
  const QString wav2 = workDir_->filePath("media-song2.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));

  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  media->requestPlayForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(media->stateForTest().playbackState,
               ISystemMediaInterface::PlaybackState::Playing);

  media->requestPauseForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u23F8"));
  QTRY_COMPARE(media->stateForTest().playbackState,
               ISystemMediaInterface::PlaybackState::Paused);

  media->requestNextForTest();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));

  media->requestPreviousForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));

  media->requestSeekForTest(777);
  QTRY_COMPARE(media->stateForTest().positionMs, 777LL);
}

void TestMainWindow::backendSignals_updateUiAndSystemMedia() {
  DummySystemMediaInterface *media =
      window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);
  PlaybackBackendManager *backend =
      window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);
  AudioPlayer *player = backend->player();
  QVERIFY(player != nullptr);
  DummyAudioPlayer *dummyPlayer = qobject_cast<DummyAudioPlayer *>(player);
  QVERIFY(dummyPlayer != nullptr);
  QSlider *slider = window_->findChild<QSlider *>("horizontalSlider");
  QVERIFY(slider != nullptr);
  QStatusBar *statusBar = window_->findChild<QStatusBar *>("statusbar");
  QVERIFY(statusBar != nullptr);

  const QString wav = workDir_->filePath("signal-song.wav");
  QVERIFY(writeSilentWav(wav));

  dummyPlayer->setDurationForTest(125000);
  player->setSource(QUrl::fromLocalFile(wav));
  QTRY_COMPARE(slider->maximum(), 125000);
  QTRY_COMPARE(media->stateForTest().durationMs, 125000LL);
  QTRY_COMPARE(statusBar->currentMessage(), QString());

  player->setPosition(65000);
  QTRY_COMPARE(slider->value(), 65000);
  QTRY_COMPARE(media->stateForTest().positionMs, 65000LL);
  QTRY_COMPARE(statusBar->currentMessage(), QString());
}

void TestMainWindow::systemMediaToggleRequest_togglesPlayback() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  DummySystemMediaInterface *media =
      window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);
  QStatusBar *statusBar = window_->findChild<QStatusBar *>("statusbar");
  QVERIFY(statusBar != nullptr);

  const QString wav = workDir_->filePath("toggle-song.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));
  PlaybackBackendManager *backend =
      window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);
  DummyAudioPlayer *dummyPlayer =
      qobject_cast<DummyAudioPlayer *>(backend->player());
  QVERIFY(dummyPlayer != nullptr);
  dummyPlayer->setDurationForTest(125000);

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  media->requestPlayForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));

  backend->player()->setPosition(65000);
  QTRY_COMPARE(statusBar->currentMessage(), QString("01:05 / 02:05"));

  media->requestToggleForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u23F8"));
  QTRY_COMPARE(media->stateForTest().playbackState,
               ISystemMediaInterface::PlaybackState::Paused);
  QTRY_COMPARE(statusBar->currentMessage(), QString("01:05 / 02:05"));

  media->requestToggleForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(media->stateForTest().playbackState,
               ISystemMediaInterface::PlaybackState::Playing);
  QTRY_COMPARE(statusBar->currentMessage(), QString("01:05 / 02:05"));

  media->requestPauseForTest();
  QTRY_COMPARE(statusBar->currentMessage(), QString("01:05 / 02:05"));

  QAction *stopAction = window_->findChild<QAction *>("actionStop");
  QVERIFY(stopAction != nullptr);
  stopAction->trigger();
  QTRY_COMPARE(statusBar->currentMessage(), QString());
}

void TestMainWindow::sliderReleased_invokesSeekFlow() {
  DummySystemMediaInterface *media =
      window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);
  QSlider *slider = window_->findChild<QSlider *>("horizontalSlider");
  QVERIFY(slider != nullptr);

  slider->setMaximum(1000);
  slider->setValue(222);

  QVERIFY(QMetaObject::invokeMethod(slider, "sliderReleased",
                                    Qt::DirectConnection));
  QTRY_COMPARE(media->stateForTest().positionMs, 222LL);
}

void TestMainWindow::playlistTableBackspace_removesSelectedRow() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("remove-1.wav");
  const QString wav2 = workDir_->filePath("remove-2.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  QCOMPARE(playlist->songCount(), 2);

  table->setCurrentIndex(table->model()->index(0, 0));
  table->setFocus();
  QTest::keyClick(table, Qt::Key_Backspace);
  QTRY_COMPARE(playlist->songCount(), 1);
  QCOMPARE(playlist->getSongByIndex(0).at("title").text, std::string("Song2"));
}

void TestMainWindow::queueActions_fromContextMenu_areWired() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("queue-1.wav");
  const QString wav2 = workDir_->filePath("queue-2.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QAction *playNextAction = tabs->playNextAction();
  QAction *playEndAction = tabs->playEndAction();
  QVERIFY(playNextAction != nullptr);
  QVERIFY(playEndAction != nullptr);

  const QModelIndex idx0 = table->model()->index(0, 0);
  const QModelIndex idx1 = table->model()->index(1, 0);
  playNextAction->setData(QVariant::fromValue(idx0));
  playNextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString("1"));

  playEndAction->setData(QVariant::fromValue(idx1));
  playEndAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString("2"));
}

void TestMainWindow::playbackOrderMenuActions_areExclusive() {
  QAction *defaultAction = window_->findChild<QAction *>("actionDefault");
  QAction *shuffleAction =
      window_->findChild<QAction *>("actionShuffle_tracks");
  QVERIFY(defaultAction != nullptr);
  QVERIFY(shuffleAction != nullptr);

  struct Case {
    QAction *trigger;
    QAction *checked;
    QAction *unchecked;
  };
  const std::vector<Case> cases = {
      {shuffleAction, shuffleAction, defaultAction},
      {defaultAction, defaultAction, shuffleAction},
  };

  for (const Case &c : cases) {
    c.trigger->trigger();
    QTRY_VERIFY(c.checked->isChecked());
    QVERIFY(!c.unchecked->isChecked());
  }
}

void TestMainWindow::librarySearchAction_opensDialog() {
  QAction *searchAction = window_->findChild<QAction *>("actionSearch");
  QVERIFY(searchAction != nullptr);

  searchAction->trigger();

  QTRY_VERIFY(window_->findChild<LibrarySearchDialog *>() != nullptr);
}

void TestMainWindow::librarySearchDialog_canCreateNewPlaylistTabFromResults() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString wav1 = workDir_->filePath("search-tab-song1.wav");
  const QString wav2 = workDir_->filePath("search-tab-song2.wav");
  const QString wav3 = workDir_->filePath("search-tab-song3.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  QVERIFY(writeSilentWav(wav3));

  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->addSong(makeSong("Song3", "Artist", wav3));

  QAction *searchAction = window_->findChild<QAction *>("actionSearch");
  QVERIFY(searchAction != nullptr);
  searchAction->trigger();

  LibrarySearchDialog *dialog = window_->findChild<LibrarySearchDialog *>();
  QTRY_VERIFY(dialog != nullptr);

  QLineEdit *expressionEdit = dialog->findChild<QLineEdit *>("expression_edit");
  QPushButton *searchButton = dialog->findChild<QPushButton *>("search_button");
  QPushButton *createPlaylistButton =
      dialog->findChild<QPushButton *>("create_playlist_button");
  QVERIFY(expressionEdit != nullptr);
  QVERIFY(searchButton != nullptr);
  QVERIFY(createPlaylistButton != nullptr);

  expressionEdit->setText("title IS song2");
  QTest::mouseClick(searchButton, Qt::LeftButton);
  QTRY_VERIFY(createPlaylistButton->isEnabled());

  const int oldTabCount = tabs->tabWidget()->count();
  QTest::mouseClick(createPlaylistButton, Qt::LeftButton);

  QTRY_COMPARE(tabs->tabWidget()->count(), oldTabCount + 1);
  QTRY_COMPARE(tabs->currentPlaylist()->songCount(), 1);
  QCOMPARE(tabs->currentPlaylist()->getSongByIndex(0).at("title").text,
           std::string("Song2"));
}

void TestMainWindow::playStats_seekToEndWithoutListenDoesNotCount() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  PlaybackBackendManager *backend =
      window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);
  DummyAudioPlayer *dummyPlayer =
      qobject_cast<DummyAudioPlayer *>(backend->player());
  QVERIFY(dummyPlayer != nullptr);
  QSlider *slider = window_->findChild<QSlider *>("horizontalSlider");
  QVERIFY(slider != nullptr);

  const QString wav = workDir_->filePath("stats-seek.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));
  dummyPlayer->setDurationForTest(120000);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QVERIFY(playAction != nullptr);
  playAction->trigger();

  slider->setMaximum(120000);
  slider->setValue(115000);
  QVERIFY(QMetaObject::invokeMethod(slider, "sliderReleased",
                                    Qt::DirectConnection));

  QSqlDatabase db = QSqlDatabase::database("myplayer_main");
  QVERIFY(db.isValid());
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT IFNULL(MAX(play_count), 0) FROM song_play_stats"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 0);
}

void TestMainWindow::playStats_nearEndWithListenCountsOnceAndRefreshes() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  PlaybackBackendManager *backend =
      window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);
  DummyAudioPlayer *dummyPlayer =
      qobject_cast<DummyAudioPlayer *>(backend->player());
  QVERIFY(dummyPlayer != nullptr);

  const QString wav = workDir_->filePath("stats-listen.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));
  dummyPlayer->setDurationForTest(120000);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QVERIFY(playAction != nullptr);
  playAction->trigger();

  backend->player()->setPosition(1000);
  backend->player()->setPosition(35000);
  backend->player()->setPosition(70000);
  backend->player()->setPosition(109000);
  backend->player()->setPosition(115000);
  backend->player()->setPosition(119000);

  QSqlDatabase db = QSqlDatabase::database("myplayer_main");
  QVERIFY(db.isValid());
  QSqlQuery q(db);
  QVERIFY(q.exec("SELECT COUNT(*) FROM song_play_stats"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);

  QVERIFY(q.exec("SELECT play_count, last_played_timestamp "
                 "FROM song_play_stats LIMIT 1"));
  QVERIFY(q.next());
  QCOMPARE(q.value(0).toInt(), 1);
  QVERIFY(q.value(1).toLongLong() > 0);
}

void TestMainWindow::cloudSync_startupPull_appliesCloudPlayCount_integration() {
  delete window_;
  window_ = nullptr;

  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSyncService::clearDummyPullPages();
  CloudPlayStatsSyncService::clearDummyPushCalls();

  const QString dbPath = workDir_->filePath("myplayer.db");
  const QString seedConn =
      QStringLiteral("seed_mainwindow_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  {
    QSqlDatabase seedDb = QSqlDatabase::addDatabase("QSQLITE", seedConn);
    seedDb.setDatabaseName(dbPath);
    QVERIFY(seedDb.open());
    QSqlQuery q(seedDb);
    QVERIFY(q.exec("PRAGMA foreign_keys = ON"));
    QVERIFY(q.exec("INSERT OR REPLACE INTO song_identities(identity_id, "
                   "song_identity_key) VALUES (1, 'song|artist|album')"));
    QVERIFY(q.exec("INSERT OR REPLACE INTO songs(song_id, title, artist, "
                   "album, discnumber, tracknumber, date, genre, filepath, "
                   "identity_id) VALUES "
                   "(1, 'Song', 'Artist', 'Album', 1, 1, '2024-01-01', "
                   "'genre', '/tmp/cloud-seeded.wav', 1)"));
    QVERIFY(q.exec("INSERT OR REPLACE INTO playlists(playlist_id, name, "
                   "last_played) VALUES (1, 'Default Playlist', 1)"));
    QVERIFY(
        q.exec("INSERT OR REPLACE INTO playlist_items(playlist_id, song_id, "
               "position) VALUES (1, 1, 1)"));
    seedDb.close();
  }
  QSqlDatabase::removeDatabase(seedConn);

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue("cloud_sync/last_synced_at", 1000);
  settings.setValue("cloud_sync/rebase_pending", false);

  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 7,
                         .updatedAt = 2000};
  CloudPlayStatsSyncService::setDummyPullPages({{item}}, true, 2000);

  window_ = new MainWindow();
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTRY_COMPARE(playlist->songCount(), 1);
  QTRY_COMPARE(playlist->getSongByIndex(0).at("play_count").text,
               std::string("7"));

  const qint64 syncedAt =
      settings.value("cloud_sync/last_synced_at", 0).toLongLong();
  QVERIFY(syncedAt >= 2000);

  CloudPlayStatsSyncService::clearDummyPullPages();
  CloudPlayStatsSyncService::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"

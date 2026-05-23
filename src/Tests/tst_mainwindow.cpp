#include <QAbstractButton>
#include <QAction>
#include <QDataStream>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMainWindow>
#include <QObject>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QSignalSpy>
#include <QSlider>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QStandardPaths>
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
#include "../settingsdialog.h"
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
  song.insert_or_assign("title", FieldValue(title.toStdString(), "title"));
  song.insert_or_assign("artist", FieldValue(artist.toStdString(), "artist"));
  song.insert_or_assign("album", FieldValue("Album", "album"));
  song.insert_or_assign("discnumber", FieldValue("1", "discnumber"));
  song.insert_or_assign("tracknumber", FieldValue("1", "tracknumber"));
  song.insert_or_assign("date", FieldValue("2024-01-01", "date"));
  song.insert_or_assign("genre", FieldValue("genre", "genre"));
  song.insert_or_assign("filepath",
                        FieldValue(filepath.toStdString(), "filepath"));
  const std::string identity =
      util::normalizedText(title).toStdString() + "|" +
      util::normalizedText(artist).toStdString() + "|" +
      util::normalizedText(QStringLiteral("Album")).toStdString();
  song.insert_or_assign("song_identity_key",
                        FieldValue(identity, "song_identity_key"));
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
  void deferredFileBackedStartup_disablesActionsAndIgnoresMediaRequests();
  void systemMediaRequests_areWired();
  void backendSignals_updateUiAndSystemMedia();
  void systemMediaToggleRequest_togglesPlayback();
  void windowTitleExpression_updatesOnPlaybackAndResetsOnStop();
  void settingsDisplayPreview_usesCurrentSongContext();
  void sliderReleased_invokesSeekFlow();
  void volumeSlider_defaultsPropagatesAndPersists();
  void playlistTableBackspace_removesSelectedRow();
  void queueActions_fromContextMenu_areWired();
  void playbackOrderMenuActions_areExclusive();
  void playbackOrderMenuActions_persistToSettings();
  void playbackOrderMenuActions_restoreFromSettingsOnStartup();
  void cursorFollowsPlaybackAction_persistsToSettings();
  void cursorFollowsPlaybackAction_controlsSelectionOnSongSwitch();
  void playbackFollowsCursorAction_persistsToSettings();
  void playbackFollowsCursorAction_playsSelectedRowWhenQueueEmpty();
  void playbackFollowsCursorAction_ignoresCurrentSongSelection();
  void playbackFollowsCursorAction_queuePreemptsSelectionAndCursorFollows();
  void playbackFollowsCursorAction_queuePreemptsSelectionWithoutCursorFollow();
  void librarySearchAction_opensDialog();
  void librarySearchDialog_canCreateNewPlaylistTabFromResults();
  void playStats_seekToEndWithoutListenDoesNotCount();
  void playStats_nearEndWithListenCountsOnceAndRefreshes();
  void cloudSync_startupRebase_runsOnWindowStartup();
  void cloudSync_manualRebase_appliesCloudPlayCount_integration();

private:
  void recreateWindow();
  MainWindow *window_ = nullptr;
  QTemporaryDir *workDir_ = nullptr;
  QString oldCwd_;
  QString connectionName_;
};

void TestMainWindow::recreateWindow() {
  delete window_;
  window_ = nullptr;
  connectionName_ =
      QStringLiteral("mainwindow_test_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  window_ = new MainWindow(":memory:", connectionName_);
}

void TestMainWindow::init() {
  qputenv("MYPLAYER_USE_DUMMY_AUDIO_PLAYER", "1");
  qputenv("MYPLAYER_USE_DUMMY_MEDIA_INTERFACE", "1");
  {
    QSettings settings;
    settings.clear();
    settings.setValue("window_title/expression",
                      StatusRuntimeSymbolTable::defaultWindowTitleExpression());
    settings.setValue("status_bar/expression",
                      StatusRuntimeSymbolTable::defaultStatusBarExpression());
    settings.sync();
  }

  workDir_ = new QTemporaryDir();
  QVERIFY(workDir_->isValid());
  oldCwd_ = QDir::currentPath();
  QVERIFY(QDir::setCurrent(workDir_->path()));

  recreateWindow();
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
  qunsetenv("MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS");
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

  QAbstractButton *playButton =
      window_->findChild<QAbstractButton *>("play_button");
  QAbstractButton *pauseButton =
      window_->findChild<QAbstractButton *>("pause_button");
  QAbstractButton *stopButton =
      window_->findChild<QAbstractButton *>("stop_button");
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

void TestMainWindow::
    deferredFileBackedStartup_disablesActionsAndIgnoresMediaRequests() {
  delete window_;
  window_ = nullptr;
  qputenv("MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS", "300");

  connectionName_ =
      QStringLiteral("mainwindow_file_test_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  const QString dbPath = workDir_->filePath("deferred-startup.sqlite");
  window_ = new MainWindow(dbPath, connectionName_);

  QAction *openAction = window_->findChild<QAction *>("actionOpen");
  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *manualRebase =
      window_->findChild<QAction *>("actionManual_cloud_rebase");
  QStatusBar *statusBar = window_->findChild<QStatusBar *>("statusbar");
  auto *media = window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(openAction != nullptr);
  QVERIFY(playAction != nullptr);
  QVERIFY(manualRebase != nullptr);
  QVERIFY(statusBar != nullptr);
  QVERIFY(media != nullptr);

  QVERIFY(!openAction->isEnabled());
  QVERIFY(!playAction->isEnabled());
  QVERIFY(!manualRebase->isEnabled());
  QCOMPARE(statusBar->currentMessage(), QStringLiteral("Loading library..."));

  media->requestPlayForTest();
  media->requestNextForTest();
  media->requestPreviousForTest();
  QCOMPARE(media->stateForTest().playbackState,
           ISystemMediaInterface::PlaybackState::Stopped);

  QTRY_VERIFY(openAction->isEnabled());
  QTRY_VERIFY(playAction->isEnabled());
  QCOMPARE(statusBar->currentMessage(), QString{});
  qunsetenv("MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS");
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
  QTRY_VERIFY(statusBar->currentMessage().contains("01:05 / 02:05"));

  media->requestToggleForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u23F8"));
  QTRY_COMPARE(media->stateForTest().playbackState,
               ISystemMediaInterface::PlaybackState::Paused);
  QTRY_VERIFY(statusBar->currentMessage().contains("01:05 / 02:05"));

  media->requestToggleForTest();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(media->stateForTest().playbackState,
               ISystemMediaInterface::PlaybackState::Playing);
  QTRY_VERIFY(statusBar->currentMessage().contains("01:05 / 02:05"));

  media->requestPauseForTest();
  QTRY_VERIFY(statusBar->currentMessage().contains("01:05 / 02:05"));

  QAction *stopAction = window_->findChild<QAction *>("actionStop");
  QVERIFY(stopAction != nullptr);
  stopAction->trigger();
  QTRY_COMPARE(statusBar->currentMessage(), QString());
}

void TestMainWindow::windowTitleExpression_updatesOnPlaybackAndResetsOnStop() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString initialTitle = window_->windowTitle();
  const QString wav = workDir_->filePath("window-title-song.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *stopAction = window_->findChild<QAction *>("actionStop");
  QVERIFY(playAction != nullptr);
  QVERIFY(stopAction != nullptr);

  playAction->trigger();
  QTRY_VERIFY(window_->windowTitle() != initialTitle);
  QTRY_VERIFY(!window_->windowTitle().isEmpty());

  stopAction->trigger();
  QTRY_COMPARE(window_->windowTitle(), initialTitle);
}

void TestMainWindow::settingsDisplayPreview_usesCurrentSongContext() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString wav = workDir_->filePath("settings-preview-song.wav");
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

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QVERIFY(playAction != nullptr);
  playAction->trigger();

  QAction *settingsAction = window_->findChild<QAction *>("actionSettings");
  QVERIFY(settingsAction != nullptr);
  settingsAction->trigger();

  SettingsDialog *dialog = window_->findChild<SettingsDialog *>();
  QTRY_VERIFY(dialog != nullptr);
  QPlainTextEdit *statusEdit =
      dialog->findChild<QPlainTextEdit *>("status_expression_edit");
  QLabel *preview =
      dialog->findChild<QLabel *>("display_expression_preview_value_label");
  QVERIFY(statusEdit != nullptr);
  QVERIFY(preview != nullptr);

  statusEdit->setPlainText("`${artist} - ${title}`");
  const MSong &playedSong = playlist->getSongByIndex(0);
  const QString expectedArtistTitle =
      QStringLiteral("%1 - %2")
          .arg(QString::fromStdString(playedSong.at("artist").text))
          .arg(QString::fromStdString(playedSong.at("title").text));
  QTRY_COMPARE(preview->text(), expectedArtistTitle);

  statusEdit->setPlainText("`${playback_time}`");
  backend->player()->setPosition(65000);
  QTRY_COMPARE(preview->text(), QString("01:05"));
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

void TestMainWindow::volumeSlider_defaultsPropagatesAndPersists() {
  QSlider *volumeSlider = window_->findChild<QSlider *>("volumeSlider");
  QVERIFY(volumeSlider != nullptr);
  QCOMPARE(volumeSlider->minimum(), 0);
  QCOMPARE(volumeSlider->maximum(), 100);
  QCOMPARE(volumeSlider->value(), 100);

  PlaybackBackendManager *backend =
      window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);
  DummyAudioPlayer *dummyPlayer =
      qobject_cast<DummyAudioPlayer *>(backend->player());
  QVERIFY(dummyPlayer != nullptr);
  QCOMPARE(dummyPlayer->volumeForTest(), 100);

  volumeSlider->setValue(37);
  QCOMPARE(dummyPlayer->volumeForTest(), 37);

  {
    QSettings settings;
    QCOMPARE(settings.value("playback/volume_percent").toInt(), 37);
  }

  recreateWindow();
  volumeSlider = window_->findChild<QSlider *>("volumeSlider");
  QVERIFY(volumeSlider != nullptr);
  QCOMPARE(volumeSlider->value(), 37);

  backend = window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);
  dummyPlayer = qobject_cast<DummyAudioPlayer *>(backend->player());
  QVERIFY(dummyPlayer != nullptr);
  QCOMPARE(dummyPlayer->volumeForTest(), 37);
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

void TestMainWindow::playbackOrderMenuActions_persistToSettings() {
  QAction *defaultAction = window_->findChild<QAction *>("actionDefault");
  QAction *shuffleAction =
      window_->findChild<QAction *>("actionShuffle_tracks");
  QVERIFY(defaultAction != nullptr);
  QVERIFY(shuffleAction != nullptr);

  shuffleAction->trigger();
  QTRY_VERIFY(shuffleAction->isChecked());
  {
    QSettings settings;
    QCOMPARE(settings.value("playback/order_action").toString(),
             QString("actionShuffle_tracks"));
  }

  defaultAction->trigger();
  QTRY_VERIFY(defaultAction->isChecked());
  {
    QSettings settings;
    QCOMPARE(settings.value("playback/order_action").toString(),
             QString("actionDefault"));
  }
}

void TestMainWindow::playbackOrderMenuActions_restoreFromSettingsOnStartup() {
  {
    QSettings settings;
    settings.setValue("playback/order_action", "actionShuffle_tracks");
  }

  recreateWindow();

  QAction *defaultAction = window_->findChild<QAction *>("actionDefault");
  QAction *shuffleAction =
      window_->findChild<QAction *>("actionShuffle_tracks");
  QVERIFY(defaultAction != nullptr);
  QVERIFY(shuffleAction != nullptr);
  QVERIFY(shuffleAction->isChecked());
  QVERIFY(!defaultAction->isChecked());
}

void TestMainWindow::cursorFollowsPlaybackAction_persistsToSettings() {
  QAction *cursorAction =
      window_->findChild<QAction *>("actionCursor_follows_playback");
  QVERIFY(cursorAction != nullptr);
  QVERIFY(cursorAction->isCheckable());
  QVERIFY(cursorAction->isChecked());

  cursorAction->trigger();
  QVERIFY(!cursorAction->isChecked());
  {
    QSettings settings;
    QCOMPARE(settings.value("playback/cursor_follows_playback").toBool(),
             false);
  }

  recreateWindow();
  cursorAction = window_->findChild<QAction *>("actionCursor_follows_playback");
  QVERIFY(cursorAction != nullptr);
  QVERIFY(!cursorAction->isChecked());

  cursorAction->trigger();
  QVERIFY(cursorAction->isChecked());
  {
    QSettings settings;
    QCOMPARE(settings.value("playback/cursor_follows_playback").toBool(), true);
  }
}

void TestMainWindow::
    cursorFollowsPlaybackAction_controlsSelectionOnSongSwitch() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("cursor-follow-1.wav");
  const QString wav2 = workDir_->filePath("cursor-follow-2.wav");
  const QString wav3 = workDir_->filePath("cursor-follow-3.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  QVERIFY(writeSilentWav(wav3));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->addSong(makeSong("Song3", "Artist", wav3));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *nextAction = window_->findChild<QAction *>("actionNext");
  QAction *cursorAction =
      window_->findChild<QAction *>("actionCursor_follows_playback");
  QVERIFY(playAction != nullptr);
  QVERIFY(nextAction != nullptr);
  QVERIFY(cursorAction != nullptr);

  table->setCurrentIndex(table->model()->index(0, 0));
  playAction->trigger();
  nextAction->trigger();
  QTRY_COMPARE(table->currentIndex().row(), 1);

  cursorAction->setChecked(false);
  table->setCurrentIndex(table->model()->index(0, 0));
  nextAction->trigger();
  QTRY_COMPARE(table->currentIndex().row(), 0);
}

void TestMainWindow::playbackFollowsCursorAction_persistsToSettings() {
  QAction *playbackAction =
      window_->findChild<QAction *>("actionPlayback_follows_cursor");
  QVERIFY(playbackAction != nullptr);
  QVERIFY(playbackAction->isCheckable());
  QVERIFY(!playbackAction->isChecked());

  playbackAction->trigger();
  QVERIFY(playbackAction->isChecked());
  {
    QSettings settings;
    QCOMPARE(settings.value("playback/playback_follows_cursor").toBool(), true);
  }

  recreateWindow();
  playbackAction =
      window_->findChild<QAction *>("actionPlayback_follows_cursor");
  QVERIFY(playbackAction != nullptr);
  QVERIFY(playbackAction->isChecked());

  playbackAction->trigger();
  QVERIFY(!playbackAction->isChecked());
  {
    QSettings settings;
    QCOMPARE(settings.value("playback/playback_follows_cursor").toBool(),
             false);
  }
}

void TestMainWindow::
    playbackFollowsCursorAction_playsSelectedRowWhenQueueEmpty() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("playback-follow-1.wav");
  const QString wav2 = workDir_->filePath("playback-follow-2.wav");
  const QString wav3 = workDir_->filePath("playback-follow-3.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  QVERIFY(writeSilentWav(wav3));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->addSong(makeSong("Song3", "Artist", wav3));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *nextAction = window_->findChild<QAction *>("actionNext");
  QAction *playbackFollowAction =
      window_->findChild<QAction *>("actionPlayback_follows_cursor");
  QVERIFY(playAction != nullptr);
  QVERIFY(nextAction != nullptr);
  QVERIFY(playbackFollowAction != nullptr);

  playbackFollowAction->setChecked(true);
  playAction->trigger();
  table->selectionModel()->select(table->model()->index(2, 0),
                                  QItemSelectionModel::ClearAndSelect |
                                      QItemSelectionModel::Rows);
  table->setCurrentIndex(table->model()->index(2, 0));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 2, statusCol), QString::fromUtf8("\u25B6"));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 0);

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 1);
}

void TestMainWindow::playbackFollowsCursorAction_ignoresCurrentSongSelection() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("playback-current-1.wav");
  const QString wav2 = workDir_->filePath("playback-current-2.wav");
  const QString wav3 = workDir_->filePath("playback-current-3.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  QVERIFY(writeSilentWav(wav3));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->addSong(makeSong("Song3", "Artist", wav3));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *nextAction = window_->findChild<QAction *>("actionNext");
  QAction *playbackFollowAction =
      window_->findChild<QAction *>("actionPlayback_follows_cursor");
  QAction *cursorFollowAction =
      window_->findChild<QAction *>("actionCursor_follows_playback");
  QVERIFY(playAction != nullptr);
  QVERIFY(nextAction != nullptr);
  QVERIFY(playbackFollowAction != nullptr);
  QVERIFY(cursorFollowAction != nullptr);

  playbackFollowAction->setChecked(true);
  cursorFollowAction->setChecked(false);
  playAction->trigger();
  table->selectionModel()->select(table->model()->index(0, 0),
                                  QItemSelectionModel::ClearAndSelect |
                                      QItemSelectionModel::Rows);
  table->setCurrentIndex(table->model()->index(0, 0));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 2, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 0);
}

void TestMainWindow::
    playbackFollowsCursorAction_queuePreemptsSelectionAndCursorFollows() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("playback-queue-both-1.wav");
  const QString wav2 = workDir_->filePath("playback-queue-both-2.wav");
  const QString wav3 = workDir_->filePath("playback-queue-both-3.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  QVERIFY(writeSilentWav(wav3));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->addSong(makeSong("Song3", "Artist", wav3));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *nextAction = window_->findChild<QAction *>("actionNext");
  QAction *playbackFollowAction =
      window_->findChild<QAction *>("actionPlayback_follows_cursor");
  QAction *cursorFollowAction =
      window_->findChild<QAction *>("actionCursor_follows_playback");
  QVERIFY(playAction != nullptr);
  QVERIFY(nextAction != nullptr);
  QVERIFY(playbackFollowAction != nullptr);
  QVERIFY(cursorFollowAction != nullptr);
  QVERIFY(tabs->playNextAction() != nullptr);

  playbackFollowAction->setChecked(true);
  cursorFollowAction->setChecked(true);
  playAction->trigger();
  table->selectionModel()->select(table->model()->index(2, 0),
                                  QItemSelectionModel::ClearAndSelect |
                                      QItemSelectionModel::Rows);
  table->setCurrentIndex(table->model()->index(2, 0));
  tabs->playNextAction()->setData(
      QVariant::fromValue(table->model()->index(1, 0)));
  tabs->playNextAction()->trigger();

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 1);

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 2, statusCol), QString::fromUtf8("\u25B6"));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 0);

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 1);
}

void TestMainWindow::
    playbackFollowsCursorAction_queuePreemptsSelectionWithoutCursorFollow() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QTableView *table =
      tabs->tabWidget()->currentWidget()->findChild<QTableView *>();
  QVERIFY(table != nullptr);

  const QString wav1 = workDir_->filePath("playback-queue-playback-1.wav");
  const QString wav2 = workDir_->filePath("playback-queue-playback-2.wav");
  const QString wav3 = workDir_->filePath("playback-queue-playback-3.wav");
  QVERIFY(writeSilentWav(wav1));
  QVERIFY(writeSilentWav(wav2));
  QVERIFY(writeSilentWav(wav3));
  playlist->addSong(makeSong("Song1", "Artist", wav1));
  playlist->addSong(makeSong("Song2", "Artist", wav2));
  playlist->addSong(makeSong("Song3", "Artist", wav3));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  const int statusCol = statusColumn(playlist);
  QVERIFY(statusCol >= 0);

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QAction *nextAction = window_->findChild<QAction *>("actionNext");
  QAction *playbackFollowAction =
      window_->findChild<QAction *>("actionPlayback_follows_cursor");
  QAction *cursorFollowAction =
      window_->findChild<QAction *>("actionCursor_follows_playback");
  QVERIFY(playAction != nullptr);
  QVERIFY(nextAction != nullptr);
  QVERIFY(playbackFollowAction != nullptr);
  QVERIFY(cursorFollowAction != nullptr);
  QVERIFY(tabs->playNextAction() != nullptr);

  playbackFollowAction->setChecked(true);
  cursorFollowAction->setChecked(false);
  playAction->trigger();
  table->selectionModel()->select(table->model()->index(2, 0),
                                  QItemSelectionModel::ClearAndSelect |
                                      QItemSelectionModel::Rows);
  table->setCurrentIndex(table->model()->index(2, 0));
  tabs->playNextAction()->setData(
      QVariant::fromValue(table->model()->index(1, 0)));
  tabs->playNextAction()->trigger();

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 2);

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 2, statusCol), QString::fromUtf8("\u25B6"));

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 0, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 2);

  nextAction->trigger();
  QTRY_COMPARE(statusAt(playlist, 1, statusCol), QString::fromUtf8("\u25B6"));
  QTRY_COMPARE(table->currentIndex().row(), 2);
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

  QSqlDatabase db = QSqlDatabase::database(connectionName_);
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

  QSqlDatabase db = QSqlDatabase::database(connectionName_);
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

void TestMainWindow::cloudSync_startupRebase_runsOnWindowStartup() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  {
    QSettings settings;
    settings.setValue("cloud_sync/user_uuid",
                      "11111111-1111-1111-1111-111111111111");
    settings.setValue("cloud_sync/last_synced_at", 1000);
    settings.setValue("cloud_sync/rebase_pending", true);
  }

  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 7,
                         .updatedAt = 2000};
  CloudPlayStatsSync::setDummyPullPages({{item}}, true, 2000);

  recreateWindow();

  QSettings settings;
  QTRY_VERIFY(!settings.value("cloud_sync/rebase_pending", true).toBool());
  const qint64 syncedAt =
      settings.value("cloud_sync/last_synced_at", 0).toLongLong();
  QVERIFY(syncedAt >= 2000);

  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

void TestMainWindow::
    cloudSync_manualRebase_appliesCloudPlayCount_integration() {
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();

  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);

  const QString wav = workDir_->filePath("cloud-sync-song.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  QCOMPARE(playlist->songCount(), 1);

  QSettings settings;
  settings.setValue("cloud_sync/user_uuid",
                    "11111111-1111-1111-1111-111111111111");
  settings.setValue("cloud_sync/last_synced_at", 1000);
  settings.setValue("cloud_sync/rebase_pending", true);

  CloudPlayStatItem item{.songIdentityKey = "song|artist|album",
                         .playCount = 7,
                         .updatedAt = 2000};
  CloudPlayStatsSync::setDummyPullPages({{item}}, true, 2000);

  QAction *manualRebase =
      window_->findChild<QAction *>("actionManual_cloud_rebase");
  QVERIFY(manualRebase != nullptr);
  manualRebase->setEnabled(true);
  manualRebase->trigger();

  QTRY_VERIFY(playlist->getSongByIndex(0).contains("play_count"));
  QTRY_COMPARE(playlist->getSongByIndex(0).at("play_count").text,
               std::string("7"));

  const qint64 syncedAt =
      settings.value("cloud_sync/last_synced_at", 0).toLongLong();
  QVERIFY(syncedAt >= 2000);

  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"

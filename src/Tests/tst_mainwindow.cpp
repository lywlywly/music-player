#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QKeySequence>
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
#include <QStandardPaths>
#include <QStatusBar>
#include <QTableView>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>
#include <QUuid>

#include "../cloudplaystatssyncservice.h"
#include "../columnregistry.h"
#include "../databasemanager.h"
#include "../displayexpression.h"
#include "../dummyaudioplayer.h"
#include "../dummysystemmediainterface.h"
#include "../librarysearchdialog.h"
#include "../lyricsmanager.h"
#include "../mainwindow.h"
#include "../playbackbackendmanager.h"
#include "../playlisttablemodel.h"
#include "../playlisttabs.h"
#include "../settingsdialog.h"
#include "../songparser.h"
#include "../utils.h"
#include "scopedhomedir.h"
#include "testaudiofiles.h"

namespace {
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

const QAbstractItemModel *modelFor(const Playlist *playlist) {
  for (QWidget *widget : QApplication::allWidgets()) {
    auto *view = qobject_cast<QTableView *>(widget);
    if (view == nullptr) {
      continue;
    }
    auto *model = qobject_cast<PlaylistTableModel *>(view->model());
    if (model != nullptr && model->playlist() == playlist) {
      return model;
    }
  }
  return nullptr;
}

int statusColumn(const Playlist *playlist) {
  const QAbstractItemModel *model = modelFor(playlist);
  if (model == nullptr) {
    return -1;
  }
  for (int col = 0; col < model->columnCount(); ++col) {
    if (model->headerData(col, Qt::Horizontal, Qt::DisplayRole).toString() ==
        "Status") {
      return col;
    }
  }
  return -1;
}

QString statusAt(const Playlist *playlist, int row, int statusCol) {
  const QAbstractItemModel *model = modelFor(playlist);
  if (model == nullptr) {
    return {};
  }
  return model->data(model->index(row, statusCol), Qt::DisplayRole).toString();
}
} // namespace

class TestMainWindow : public QObject {
  Q_OBJECT

private slots:
  void init();
  void cleanup();

  void fileOpenAction_usesStandardShortcut();
  void preferencesAction_usesNonMacShortcut();
  void menuPlaybackActions_areWired();
  void playbackButtons_areWired();
  void clearPlaylistAction_clearsCurrentPlaylist();
  void deferredFileBackedStartup_disablesActionsAndIgnoresMediaRequests();
  void systemMediaRequests_areWired();
  void backendSignals_updateUiAndSystemMedia();
  void systemMediaToggleRequest_togglesPlayback();
  void songStart_publishesTitleArtistAndArtworkToSystemMedia();
  void windowTitleExpression_updatesOnPlaybackAndResetsOnStop();
  void settingsDisplayPreview_usesCurrentSongContext();
  void settingsApply_updatesStatusBarAndWindowTitle();
  void startup_displayExpressionResolvesCustomField();
  void sliderReleased_invokesSeekFlow();
  void volumeSlider_defaultsPropagatesAndPersists();
  void queueActions_fromContextMenu_areWired();
  void playbackOrderMenuActions_areExclusive();
  void playbackOrderMenuActions_persistToSettings();
  void playbackOrderMenuActions_restoreFromSettingsOnStartup();
  void cursorFollowsPlaybackAction_persistsToSettings();
  void cursorFollowsPlaybackAction_controlsSelectionOnSongSwitch();
  void playbackFollowsCursorAction_persistsToSettings();
  void playbackFollowsCursorAction_playsSelectedRowWhenQueueEmpty();
  void librarySearchAction_opensDialog();
  void librarySearchDialog_canCreateNewPlaylistTabFromResults();
  void countedPlay_isPushedToCloud();
  void cloudSync_startupRebase_runsOnWindowStartup();
  void cloudSync_manualRebase_appliesCloudPlayCount_integration();

  void play_allFilesMissing_stopsAndReportsError();
  void lyrics_followPositionAfterBackendSwitch();

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
    settings.clear(); // default display expressions
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

void TestMainWindow::fileOpenAction_usesStandardShortcut() {
  QAction *openAction = window_->findChild<QAction *>("actionOpen");
  QVERIFY(openAction != nullptr);
  QCOMPARE(openAction->shortcut(), QKeySequence(QKeySequence::Open));
}

void TestMainWindow::preferencesAction_usesNonMacShortcut() {
  QAction *preferencesAction =
      window_->findChild<QAction *>("actionPreferences");
  QVERIFY(preferencesAction != nullptr);
#ifdef Q_OS_MACOS
  QVERIFY(preferencesAction->shortcut().isEmpty());
#else
  QCOMPARE(preferencesAction->shortcut(), QKeySequence(Qt::CTRL | Qt::Key_P));
#endif
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

void TestMainWindow::songStart_publishesTitleArtistAndArtworkToSystemMedia() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  auto *media = window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);

  // playSong re-reads the tags, so the title and artist are in the file.
  const QString wav = workDir_->filePath("media-cover.wav");
  const QByteArray cover("cover-bytes");
  QVERIFY(writeWavWithCover(wav, cover, "image/png"));
  QVERIFY(SongParser::writeTags(wav.toStdString(),
                                {{"title", "Tagged"}, {"artist", "Singer"}},
                                ColumnRegistry()));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  media->requestPlayForTest();

  QTRY_COMPARE(media->stateForTest().title, QStringLiteral("Tagged"));
  QCOMPARE(media->stateForTest().artist, QStringLiteral("Singer"));
  // Set after the title, which clears the artwork.
  QCOMPARE(media->stateForTest().artworkData, cover);
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

  QAction *settingsAction = window_->findChild<QAction *>("actionPreferences");
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

void TestMainWindow::settingsApply_updatesStatusBarAndWindowTitle() {
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  QStatusBar *statusBar = window_->findChild<QStatusBar *>("statusbar");
  QVERIFY(statusBar != nullptr);

  const QString wav = workDir_->filePath("settings-apply-song.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));
  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QVERIFY(playAction != nullptr);
  playAction->trigger();
  const QString title =
      QString::fromStdString(playlist->getSongByIndex(0).at("title").text);

  QAction *settingsAction = window_->findChild<QAction *>("actionPreferences");
  QVERIFY(settingsAction != nullptr);
  settingsAction->trigger();
  SettingsDialog *dialog = window_->findChild<SettingsDialog *>();
  QTRY_VERIFY(dialog != nullptr);
  QPlainTextEdit *statusEdit =
      dialog->findChild<QPlainTextEdit *>("status_expression_edit");
  QPlainTextEdit *titleEdit =
      dialog->findChild<QPlainTextEdit *>("window_title_expression_edit");
  QVERIFY(statusEdit != nullptr);
  QVERIFY(titleEdit != nullptr);
  statusEdit->setPlainText("`status: ${title}`");
  titleEdit->setPlainText("`title: ${title}`");
  dialog->accept();

  QTRY_COMPARE(statusBar->currentMessage(), "status: " + title);
  QTRY_COMPARE(window_->windowTitle(), "title: " + title);
}

void TestMainWindow::startup_displayExpressionResolvesCustomField() {
  delete window_;
  window_ = nullptr;
  // The custom column exists in the database before the window is created,
  // so it is only known once the library has loaded.
  const QString dbPath = workDir_->filePath("custom-field.sqlite");
  const ColumnDefinition moodColumn{.id = "attr:mood", .title = "Mood"};
  const FieldDefinition moodField{.id = "attr:mood",
                                  .source = ColumnSource::SongAttribute,
                                  .valueType = ValueType::Text,
                                  .searchable = true,
                                  .writable = true};
  const QString setupConnection = QStringLiteral("custom_field_setup");
  const QString wav = workDir_->filePath("custom-field-song.wav");
  QVERIFY(writeSilentWav(wav));
  {
    ColumnRegistry registry;
    DatabaseManager databaseManager(registry, dbPath, setupConnection);
    QVERIFY(registry.upsertCustomTagDefinition(databaseManager.db(), moodColumn,
                                               moodField));
    QVERIFY(registry.loadDynamicColumns(databaseManager.db()));
    QVERIFY(SongParser::writeTags(wav.toStdString(), {{"mood", "happy"}},
                                  registry));
  }
  QSqlDatabase::removeDatabase(setupConnection);
  saveDisplayExpressionSetting(DisplayExpressionKind::StatusBar,
                               QStringLiteral("`[${mood}]`"));

  connectionName_ =
      QStringLiteral("mainwindow_file_test_%1")
          .arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
  window_ = new MainWindow(dbPath, connectionName_);
  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QStatusBar *statusBar = window_->findChild<QStatusBar *>("statusbar");
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(playAction != nullptr);
  QVERIFY(statusBar != nullptr);
  QVERIFY(tabs != nullptr);
  QTRY_VERIFY(playAction->isEnabled()); // library loaded

  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));
  playAction->trigger();

  QTRY_COMPARE(statusBar->currentMessage(), QStringLiteral("[happy]"));
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

void TestMainWindow::countedPlay_isPushedToCloud() {
  // When a play counts is decided by PlaybackController
  // (tst_playbackcontroller); the window pushes each count to cloud sync.
  qputenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC", "1");
  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  {
    QSettings settings;
    settings.setValue("cloud_sync/user_uuid",
                      "11111111-1111-1111-1111-111111111111");
  }
  recreateWindow();

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
  // playSong refreshes the song from the untagged file, which sets its
  // identity key.
  const std::string identityKey =
      playlist->getSongByIndex(0).at("song_identity_key").text;
  for (const qint64 positionMs : {1000, 35000, 70000, 109000, 119000}) {
    backend->player()->setPosition(positionMs);
  }

  QTRY_COMPARE(CloudPlayStatsSync::dummyPushCalls().size(), size_t{1});
  const CloudPlayStatsSync::DummyPushCall call =
      CloudPlayStatsSync::dummyPushCalls().front();
  QCOMPARE(call.songIdentityKey, identityKey);
  QCOMPARE(call.delta, 1);

  CloudPlayStatsSync::clearDummyPullPages();
  CloudPlayStatsSync::clearDummyPushCalls();
  qunsetenv("MYPLAYER_USE_DUMMY_CLOUD_SYNC");
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

void TestMainWindow::play_allFilesMissing_stopsAndReportsError() {
  // When playback gives up is tested in tst_playbackcontroller; the window
  // reports the file in the status bar and stops system media.
  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  auto *media = window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);
  QStatusBar *statusBar = window_->findChild<QStatusBar *>("statusbar");
  QVERIFY(statusBar != nullptr);

  playlist->addSong(
      makeSong("Missing", "Artist", workDir_->filePath("missing.wav")));
  playlist->setLastPlayed(playlist->getPkByIndex(0));

  QAction *playAction = window_->findChild<QAction *>("actionPlay");
  QVERIFY(playAction != nullptr);
  playAction->trigger();

  QTRY_VERIFY(statusBar->currentMessage().startsWith("Cannot play file:"));
  QVERIFY(statusBar->currentMessage().contains("missing.wav"));
  QCOMPARE(media->stateForTest().playbackState,
           ISystemMediaInterface::PlaybackState::Stopped);
}

void TestMainWindow::lyrics_followPositionAfterBackendSwitch() {
  const ScopedHomeDir scopedHome(workDir_->path());

  PlaylistTabs *tabs = window_->findChild<PlaylistTabs *>("playlistTabs");
  QVERIFY(tabs != nullptr);
  Playlist *playlist = tabs->currentPlaylist();
  QVERIFY(playlist != nullptr);
  auto *media = window_->findChild<DummySystemMediaInterface *>();
  QVERIFY(media != nullptr);
  auto *lyrics = window_->findChild<LyricsManager *>();
  QVERIFY(lyrics != nullptr);
  PlaybackBackendManager *backend =
      window_->findChild<PlaybackBackendManager *>();
  QVERIFY(backend != nullptr);

  const QString wav = workDir_->filePath("lyrics-song.wav");
  QVERIFY(writeSilentWav(wav));
  playlist->addSong(makeSong("Song", "Artist", wav));
  playlist->setLastPlayed(playlist->getPkByIndex(0));
  // playSong refreshes the song from the untagged file, so look up the title
  // and artist it plays with. The .lrc file is written after this first play,
  // so only the play on the new backend loads it.
  media->requestPlayForTest();
  const MSong &played = playlist->getSongByIndex(0);
  QVERIFY(QDir(workDir_->path()).mkpath("Music"));
  QFile lrc(
      QDir(workDir_->path())
          .filePath(QStringLiteral("Music/%1 - %2.lrc")
                        .arg(QString::fromStdString(played.at("artist").text),
                             QString::fromStdString(played.at("title").text))));
  QVERIFY(lrc.open(QIODevice::WriteOnly));
  lrc.write("[00:01.00]first\n[00:02.00]second\n");
  lrc.close();

  QAction *settingsAction = window_->findChild<QAction *>("actionPreferences");
  QVERIFY(settingsAction != nullptr);
  settingsAction->trigger();
  SettingsDialog *dialog = window_->findChild<SettingsDialog *>();
  QTRY_VERIFY(dialog != nullptr);
  const auto otherBackend =
      backend->currentBackend() == PlaybackBackendManager::Backend::GStreamer
          ? PlaybackBackendManager::Backend::QMediaPlayer
          : PlaybackBackendManager::Backend::GStreamer;
  emit dialog->backendChanged(otherBackend);
  QCOMPARE(backend->currentBackend(), otherBackend);
  dialog->close();

  // The backend switch stopped playback, so Play starts the song again on the
  // new player.
  media->requestPlayForTest();
  QTRY_VERIFY(!lyrics->getCurrentLyricsMap().empty());

  QSignalSpy spy(lyrics, &LyricsManager::newLyricsLineIndex);
  backend->player()->setPosition(2500);
  QTRY_COMPARE(spy.size(), 1);
  QCOMPARE(spy.at(0).at(0).toInt(), 1);
}

QTEST_MAIN(TestMainWindow)
#include "tst_mainwindow.moc"

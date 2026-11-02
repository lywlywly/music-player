#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "databasemanager.h"
#include "gstaudioplayer.h"
#include "librarysearchdialog.h"
#include "playbackbuttonstyle.h"
#include "settingsdialog.h"
#include "systemmediainterfacefactory.h"
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QMessageBox>
#include <QProgressDialog>
#include <QResource>
#include <QSettings>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QThread>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {
constexpr int kMinVolumePercent = 0;
constexpr int kMaxVolumePercent = 100;
constexpr int kDefaultVolumePercent = 100;
constexpr const char *kVolumeSettingsKey = "playback/volume_percent";
constexpr const char *kPlaybackFollowsCursorSettingsKey =
    "playback/playback_follows_cursor";
constexpr const char *kCursorFollowsPlaybackSettingsKey =
    "playback/cursor_follows_playback";

// Name filters for the audio files that Open and Add Folder pick up.
const QStringList &audioFileNameFilters() {
  static const QStringList filters = {"*.mp3", "*.flac", "*.m4a", "*.wav",
                                      "*.ogg", "*.opus", "*.alac"};
  return filters;
}

int clampVolumePercent(int volumePercent) {
  return std::clamp(volumePercent, kMinVolumePercent, kMaxVolumePercent);
}

SongLibrary::Snapshot loadSongLibrarySnapshot(const QString &databaseName) {
  // TODO: Extract DB-only library loading so startup does not need to construct
  // temporary full ColumnRegistry/SongLibrary instances on the worker thread.
#ifdef MYPLAYER_TESTING
  if (const int delayMs =
          qEnvironmentVariableIntValue("MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS");
      delayMs > 0) {
    QThread::msleep(static_cast<unsigned long>(delayMs));
  }
#endif
  ColumnRegistry registry;
  DatabaseManager databaseManager(
      registry, databaseName,
      QStringLiteral("playlist_loader_%1")
          .arg(reinterpret_cast<quintptr>(QThread::currentThread())));
  if (!registry.loadDynamicColumns(databaseManager.db())) {
    qFatal("loadSongLibrarySnapshot: failed to load dynamic columns");
  }
  SongLibrary library(registry, databaseManager);
  library.loadFromDatabase();
  return library.snapshot();
}

PlaybackBackendManager::Backend savedPlaybackBackend() {
  QSettings settings;
  return static_cast<PlaybackBackendManager::Backend>(
      settings
          .value(
              "playback/backend",
              static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer))
          .toInt());
}

void prewarmSelectedPlaybackBackend() {
  if (savedPlaybackBackend() == PlaybackBackendManager::Backend::GStreamer) {
    GstAudioPlayer::prewarmStartup();
  }
}

void setUpPlaybackButtonIcons(Ui::MainWindow *ui) {
  setIconOnlyPlaybackButton(ui->play_button,
                            QStringLiteral(":/icons/media-playback-start.svg"));
  setIconOnlyPlaybackButton(ui->pause_button,
                            QStringLiteral(":/icons/media-playback-pause.svg"));
  setIconOnlyPlaybackButton(ui->stop_button,
                            QStringLiteral(":/icons/media-playback-stop.svg"));
  setIconOnlyPlaybackButton(ui->prev_button,
                            QStringLiteral(":/icons/media-skip-backward.svg"));
  setIconOnlyPlaybackButton(ui->next_button,
                            QStringLiteral(":/icons/media-skip-forward.svg"));
  setIconOnlyPlaybackButton(
      ui->random_button, QStringLiteral(":/icons/media-playback-random.svg"));
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(QString{}, QString{}, parent) {}

MainWindow::MainWindow(QString databaseName, QString connectionName,
                       QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
      columnLayoutManager_(columnRegistry_, this),
      databaseManager_(columnRegistry_, std::move(databaseName),
                       std::move(connectionName)),
      songLibrary(columnRegistry_, databaseManager_),
      nowPlayingDisplay_(columnRegistry_),
      cloudPlayStatsSyncCoordinator_(songLibrary, this) {
  prewarmSelectedPlaybackBackend();
  Q_INIT_RESOURCE(resources);
  ui->setupUi(this);
  playlists_ = new PlaylistCollection(songLibrary, databaseManager_, this);
  playback_ = new PlaybackController(songLibrary, *playlists_, this);
  coverArt_ = new CoverArtPresenter(ui->label, this);
  lyrics_ = new LyricsPresenter(ui->lyricsPanel, this);
  applyDisplayThemeFromSettings();
  setUpPlaybackButtonIcons(ui);
  defaultWindowTitle_ = windowTitle();
  updatePlaybackTimeStatus();
  setUpMenuBar();
  initCloudSync();
  setUpPlaylist();
  initSettings();
  setUpPlayback();
  initVolumeControl();
  setUpPlaybackActions();
  lyrics_->applySettings();
  setUpSplitter();
  setupSystemMediaInterface();
}

void MainWindow::initCloudSync() {
  connect(&cloudPlayStatsSyncCoordinator_,
          &CloudPlayStatsSyncCoordinator::songDataChanged, this,
          [this](int songPk) { playlists_->notifySongChanged(songPk); });
  refreshManualCloudRebaseActionEnabled();
}

void MainWindow::setupSystemMediaInterface() {
  sysMedia = createSystemMediaInterface(this);
  connect(sysMedia, &ISystemMediaInterface::playRequested, playback_,
          &PlaybackController::play);
  connect(sysMedia, &ISystemMediaInterface::pauseRequested, playback_,
          &PlaybackController::pause);
  connect(sysMedia, &ISystemMediaInterface::toggleRequested, playback_,
          &PlaybackController::toggle);
  connect(sysMedia, &ISystemMediaInterface::nextRequested, playback_,
          &PlaybackController::next);
  connect(sysMedia, &ISystemMediaInterface::previousRequested, playback_,
          &PlaybackController::prev);
  connect(sysMedia, &ISystemMediaInterface::seekRequested, playback_,
          &PlaybackController::seek);
  connect(coverArt_, &CoverArtPresenter::artworkChanged, sysMedia,
          &ISystemMediaInterface::setArtwork);
}

void MainWindow::setUpMenuBar() {
  ui->actionOpen->setShortcut(QKeySequence::Open);
#ifndef Q_OS_MACOS
  ui->actionPreferences->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_P));
#endif

  playbackOrderMenuActionGroup = new QActionGroup(this);
  playbackOrderMenuActionGroup->setExclusive(true);
  ui->actionDefault->setData(Sequential);
  ui->actionShuffle_tracks->setData(Shuffle);
  const QList<QAction *> playbackOrderActions = {ui->actionDefault,
                                                 ui->actionShuffle_tracks};
  for (QAction *act : playbackOrderActions) {
    act->setCheckable(true);
    playbackOrderMenuActionGroup->addAction(act);
  }

  QSettings settings;
  const QString savedPlaybackOrder =
      settings.value("playback/order_action", "actionDefault").toString();
  QAction *actionToCheck = ui->actionDefault;
  for (QAction *act : playbackOrderActions) {
    if (act->objectName() == savedPlaybackOrder) {
      actionToCheck = act;
      break;
    }
  }
  actionToCheck->setChecked(true);

  // TODO: extract menu/settings persistence wiring to a settings coordinator.
  connect(playbackOrderMenuActionGroup, &QActionGroup::triggered, this,
          [](QAction *action) {
            QSettings settings;
            settings.setValue("playback/order_action", action->objectName());
          });

  ui->actionPlayback_follows_cursor->setCheckable(true);
  ui->actionPlayback_follows_cursor->setChecked(
      settings.value(kPlaybackFollowsCursorSettingsKey, false).toBool());
  connect(ui->actionPlayback_follows_cursor, &QAction::toggled, this,
          [](bool checked) {
            QSettings settings;
            settings.setValue(kPlaybackFollowsCursorSettingsKey, checked);
          });

  ui->actionCursor_follows_playback->setCheckable(true);
  ui->actionCursor_follows_playback->setChecked(
      settings.value(kCursorFollowsPlaybackSettingsKey, true).toBool());
  connect(ui->actionCursor_follows_playback, &QAction::toggled, this,
          [](bool checked) {
            QSettings settings;
            settings.setValue(kCursorFollowsPlaybackSettingsKey, checked);
          });

  connect(ui->actionSearch, &QAction::triggered, this,
          &MainWindow::openLibrarySearchDialog);
  connect(ui->actionManual_cloud_rebase, &QAction::triggered, this,
          &MainWindow::triggerManualCloudRebase);
}

void MainWindow::setUpPlaylist() {
  playlistTabs = ui->playlistTabs;
  setPlaylistDependentActionsEnabled(false);
  statusBar()->showMessage(tr("Loading library..."));

  if (databaseManager_.db().databaseName() == QStringLiteral(":memory:")) {
    QSqlDatabase &db = databaseManager_.db();
    if (!columnRegistry_.loadDynamicColumns(db)) {
      qFatal("setUpPlaylist: failed to load dynamic columns");
    }
    songLibrary.loadFromDatabase();
    finishSetUpPlaylist(songLibrary.snapshot());
    return;
  }

  auto *watcher = new QFutureWatcher<SongLibrary::Snapshot>(this);
  connect(watcher, &QFutureWatcher<SongLibrary::Snapshot>::finished, this,
          [this, watcher]() {
            SongLibrary::Snapshot snapshot = watcher->result();
            finishSetUpPlaylist(std::move(snapshot));
            watcher->deleteLater();
          });
  watcher->setFuture(QtConcurrent::run(loadSongLibrarySnapshot,
                                       databaseManager_.db().databaseName()));
}

void MainWindow::finishSetUpPlaylist(SongLibrary::Snapshot &&snapshot) {
  if (!columnRegistry_.loadDynamicColumns(databaseManager_.db())) {
    qFatal("finishSetUpPlaylist: failed to load dynamic columns");
  }
  // The expressions were parsed in the constructor, before the custom
  // columns above were loaded, so their custom fields were unresolved.
  nowPlayingDisplay_.loadExpressions();
  columnLayoutManager_.refreshFromRegistry();
  songLibrary.replaceWithSnapshot(std::move(snapshot));
  playlists_->loadAll();
  playlistTabs->init(&songLibrary, &playback_->queue(), &playback_->manager(),
                     playbackOrderMenuActionGroup, &columnRegistry_,
                     &columnLayoutManager_, playlists_);
  connect(playlistTabs, &PlaylistTabs::doubleClicked, playback_,
          [this](QModelIndex i) { playback_->playRow(i.row()); });
  // playlist operations
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionAdd_folder, &QAction::triggered, this,
          &MainWindow::openFolder);
  connect(ui->actionClear_Playlist, &QAction::triggered, this, [this]() {
    Playlist *current = playlistTabs->currentPlaylist();
    if (current != nullptr) {
      current->clear();
    }
  });
  playlistReady_ = true;
  playback_->setReady(true);
  setPlaylistDependentActionsEnabled(true);
  updatePlaybackTimeStatus();
  cloudPlayStatsSyncCoordinator_.startSync();
}

void MainWindow::setPlaylistDependentActionsEnabled(bool enabled) {
  ui->actionOpen->setEnabled(enabled);
  ui->actionAdd_folder->setEnabled(enabled);
  ui->actionClear_Playlist->setEnabled(enabled);
  ui->actionSearch->setEnabled(enabled);
  ui->actionPlay->setEnabled(enabled);
  ui->actionPause->setEnabled(enabled);
  ui->actionStop->setEnabled(enabled);
  ui->actionNext->setEnabled(enabled);
  ui->actionPrevious->setEnabled(enabled);
  ui->play_button->setEnabled(enabled);
  ui->pause_button->setEnabled(enabled);
  ui->stop_button->setEnabled(enabled);
  ui->next_button->setEnabled(enabled);
  ui->prev_button->setEnabled(enabled);
  refreshManualCloudRebaseActionEnabled();
}

void MainWindow::initSettings() {
  connect(ui->actionPreferences, &QAction::triggered, this, [this]() {
    SettingsDialog *dialog =
        new SettingsDialog(columnRegistry_, databaseManager_, this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    connect(dialog, &SettingsDialog::backendChanged, this,
            [this](PlaybackBackendManager::Backend backend) {
              playback_->setBackend(backend);
            });
    connect(dialog, &SettingsDialog::customFieldsChanged, this, [this]() {
      if (!columnRegistry_.loadDynamicColumns(databaseManager_.db())) {
        qFatal("initSettings: failed to reload dynamic columns");
      }
      // Resolve added or removed custom fields in the display expressions.
      nowPlayingDisplay_.loadExpressions();
      updatePlaybackTimeStatus();
      columnLayoutManager_.refreshFromRegistry();
      // TODO: refresh custom/computed attributes
    });
    connect(dialog, &SettingsDialog::cloudUuidChanged, this,
            [this](const QString &uuid) {
              refreshManualCloudRebaseActionEnabled();
              if (!uuid.isEmpty()) {
                cloudPlayStatsSyncCoordinator_.triggerManualRebase();
              }
            });
    connect(dialog, &SettingsDialog::lyricsFontChanged, this,
            [this](const QString &fontFamily, int pointSize) {
              lyrics_->setFont(fontFamily, pointSize);
            });
    connect(dialog, &SettingsDialog::lyricsHighlightColorChanged, this,
            [this](const QColor &color) { lyrics_->setHighlightColor(color); });
    connect(dialog, &SettingsDialog::statusBarExpressionChanged, this,
            [this](const QString &expression) {
              nowPlayingDisplay_.setExpression(DisplayExpressionKind::StatusBar,
                                               expression);
              updatePlaybackTimeStatus();
            });
    connect(dialog, &SettingsDialog::windowTitleExpressionChanged, this,
            [this](const QString &expression) {
              nowPlayingDisplay_.setExpression(
                  DisplayExpressionKind::WindowTitle, expression);
              updatePlaybackTimeStatus();
            });
    connect(dialog, &SettingsDialog::displayThemeModeChanged, this,
            [this](const QString &mode) { applyDisplayThemeMode(mode); });
    updateSettingsDisplayPreviewContext(dialog);
    dialog->show();
  });
}

void MainWindow::setUpPlayback() {
  // Created after setUpPlaylist, so a file-backed library loads on its worker
  // thread meanwhile.
  playback_->setBackend(savedPlaybackBackend());
  playback_->setShownPlaylist(
      [this]() { return playlistTabs->currentPlaylist(); });
  playback_->setPreferredNextRow([this]() {
    return ui->actionPlayback_follows_cursor->isChecked()
               ? playlistTabs->firstSelectedRow()
               : -1;
  });
  connect(playback_, &PlaybackController::songAboutToStart, this,
          [this](const MSong &song) { nowPlayingDisplay_.startSong(song); });
  connect(
      playback_, &PlaybackController::songStarted, this,
      [this](
          const MSong &song,
          const std::unordered_map<std::string, std::string> &remainingFields) {
        sysMedia->setTitleAndArtist(
            QString::fromStdString(song.at("title").text),
            QString::fromStdString(song.at("artist").text));
        lyrics_->showSong(song, remainingFields);
        // After setTitleAndArtist, which clears the system artwork.
        coverArt_->showSong(song);
        updatePlaybackTimeStatus();
      });
  connect(playback_, &PlaybackController::playbackStateChanged, this,
          &MainWindow::playbackStateChanged);
  connect(playback_, &PlaybackController::movedToRow, this,
          &MainWindow::navigateIndex);
  connect(playback_, &PlaybackController::seeking, this,
          [this](qint64 positionMs) { sysMedia->setPosition(positionMs); });
  connect(playback_, &PlaybackController::songFinished, this,
          [this]() { QApplication::alert(this); });
  connect(playback_, &PlaybackController::playCounted,
          &cloudPlayStatsSyncCoordinator_,
          &CloudPlayStatsSyncCoordinator::pushIncrementForSongPk);
  connect(
      playback_, &PlaybackController::playbackFailed, this,
      [this](const QString &filepath) {
        statusBar()->showMessage(
            tr("Cannot play file: %1").arg(QDir::toNativeSeparators(filepath)));
      });
  connect(playback_, &PlaybackController::positionChanged, this,
          &MainWindow::positionChanged);
  connect(playback_, &PlaybackController::durationChanged, this,
          &MainWindow::durationChanged);
  connect(playback_, &PlaybackController::bitrateChanged, this,
          &MainWindow::bitrateChanged);
}

void MainWindow::setUpPlaybackActions() {
  connect(ui->actionPlay, &QAction::triggered, playback_,
          &PlaybackController::play);
  connect(ui->play_button, &QAbstractButton::clicked, playback_,
          &PlaybackController::play);
  connect(ui->actionPause, &QAction::triggered, playback_,
          &PlaybackController::pause);
  connect(ui->pause_button, &QAbstractButton::clicked, playback_,
          &PlaybackController::pause);
  connect(ui->actionStop, &QAction::triggered, playback_,
          &PlaybackController::stop);
  connect(ui->stop_button, &QAbstractButton::clicked, playback_,
          &PlaybackController::stop);
  connect(ui->actionNext, &QAction::triggered, playback_,
          &PlaybackController::next);
  connect(ui->next_button, &QAbstractButton::clicked, playback_,
          &PlaybackController::next);
  connect(ui->actionPrevious, &QAction::triggered, playback_,
          &PlaybackController::prev);
  connect(ui->prev_button, &QAbstractButton::clicked, playback_,
          &PlaybackController::prev);
  connect(ui->random_button, &QAbstractButton::clicked, playback_,
          &PlaybackController::playRandom);
  connect(ui->horizontalSlider, &QSlider::sliderReleased, playback_,
          [this]() { playback_->seek(ui->horizontalSlider->value()); });
}

void MainWindow::applyDisplayThemeFromSettings() {
  QSettings settings;
  applyDisplayThemeMode(
      settings.value("display/theme_mode", QStringLiteral("system"))
          .toString()
          .trimmed()
          .toLower());
}

void MainWindow::applyDisplayThemeMode(const QString &mode) {
  QStyleHints *styleHints = QGuiApplication::styleHints();
  if (mode == QStringLiteral("dark")) {
    styleHints->setColorScheme(Qt::ColorScheme::Dark);
    setUpPlaybackButtonIcons(ui);
    return;
  }
  if (mode == QStringLiteral("light")) {
    styleHints->setColorScheme(Qt::ColorScheme::Light);
    setUpPlaybackButtonIcons(ui);
    return;
  }
  styleHints->setColorScheme(Qt::ColorScheme::Unknown);
  setUpPlaybackButtonIcons(ui);
}

void MainWindow::setUpSplitter() {
  connect(ui->splitter, &QSplitter::splitterMoved, coverArt_,
          &CoverArtPresenter::updateCoverSize);
  connect(ui->splitter_2, &QSplitter::splitterMoved, coverArt_,
          &CoverArtPresenter::updateCoverSize);
}

void MainWindow::initVolumeControl() {
  QSettings settings;
  const int savedVolume = clampVolumePercent(
      settings.value(kVolumeSettingsKey, kDefaultVolumePercent).toInt());
  ui->volumeSlider->setValue(savedVolume);
  playback_->setVolume(savedVolume);

  connect(ui->volumeSlider, &QSlider::valueChanged, this, [this](int volume) {
    const int clampedVolume = clampVolumePercent(volume);
    QSettings settings;
    settings.setValue(kVolumeSettingsKey, clampedVolume);
    playback_->setVolume(clampedVolume);
  });
}

void MainWindow::playbackStateChanged(PlaybackQueue::PlaybackStatus status) {
  ISystemMediaInterface::PlaybackState systemState =
      ISystemMediaInterface::PlaybackState::Stopped;
  switch (status) {
  case PlaybackQueue::PlaybackStatus::Playing:
    systemState = ISystemMediaInterface::PlaybackState::Playing;
    break;
  case PlaybackQueue::PlaybackStatus::Paused:
    systemState = ISystemMediaInterface::PlaybackState::Paused;
    break;
  case PlaybackQueue::PlaybackStatus::None:
    nowPlayingDisplay_.stopSong();
    lyrics_->clear();
    coverArt_->clear();
    break;
  }
  updatePlaybackTimeStatus();
  sysMedia->setPlaybackState(systemState);
}

void MainWindow::navigateIndex(int row, Playlist *pl) {
  if (!ui->actionCursor_follows_playback->isChecked()) {
    return;
  }
  playlistTabs->navigateIndex(row, pl);
}

bool MainWindow::event(QEvent *event) {
  if (event->type() == QEvent::StatusTip) {
    return true;
  }
  return QMainWindow::event(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  coverArt_->updateCoverSize();
}

void MainWindow::durationChanged(qint64 newDuration) {
  nowPlayingDisplay_.setDurationMs(newDuration);
  ui->horizontalSlider->setMaximum(newDuration);
  updatePlaybackTimeStatus();
  sysMedia->setDuration(newDuration);
}

void MainWindow::positionChanged(qint64 progress) {
  nowPlayingDisplay_.setPositionMs(progress);
  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
  updatePlaybackTimeStatus();
  sysMedia->updateCurrentPosition(progress);
  lyrics_->setPositionMs(progress);
}

void MainWindow::bitrateChanged(qint64 bitsPerSecond) {
  if (nowPlayingDisplay_.setStreamBitrateBps(bitsPerSecond)) {
    updatePlaybackTimeStatus();
  }
}

const MSong *MainWindow::currentSong() const {
  const int songPk = playback_->currentSongPk();
  if (songPk < 0) {
    return nullptr;
  }
  return &songLibrary.getSongByPK(songPk);
}

void MainWindow::updateSettingsDisplayPreviewContext(SettingsDialog *dialog) {
  if (!dialog) {
    return;
  }
  dialog->setDisplayExpressionPreviewContext(
      currentSong(), nowPlayingDisplay_.runtimeSymbols(playback_->status()));
}

void MainWindow::updatePlaybackTimeStatus() {
  const PlaybackQueue::PlaybackStatus status = playback_->status();
  const MSong *song = currentSong();
  updateSettingsDisplayPreviewContext(findChild<SettingsDialog *>());
  statusBar()->showMessage(nowPlayingDisplay_.statusBarText(status, song));
  setWindowTitle(
      nowPlayingDisplay_.windowTitle(status, song, defaultWindowTitle_));
}

void MainWindow::open() {
  if (!playlistReady_) {
    return;
  }
  const QList<QString> fileName = QFileDialog::getOpenFileNames(
      this, "Open the file",
      QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      QStringLiteral("Audio Files (%1);;All Files (*.*)")
          .arg(audioFileNameFilters().join(QLatin1Char(' '))));
  std::vector<std::string> paths;
  paths.reserve(static_cast<size_t>(fileName.size()));
  for (const QString &s : fileName) {
    paths.push_back(s.toStdString());
  }
  // A failed file is reported only when it was the only one selected; in a
  // multi-file add, failed files are skipped silently.
  std::vector<std::string> failedPaths;
  playlistTabs->currentPlaylist()->addFiles(paths, {}, &failedPaths);
  if (paths.size() == 1 && !failedPaths.empty()) {
    QMessageBox::warning(this, tr("Add File"),
                         tr("Could not read the file:\n%1")
                             .arg(QDir::toNativeSeparators(
                                 QString::fromStdString(failedPaths.front()))));
  }
}

void MainWindow::openFolder() {
  if (!playlistReady_) {
    return;
  }
  QString dirPath = QFileDialog::getExistingDirectory(
      this, "Select Folder",
      QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      QFileDialog::ShowDirsOnly);

  if (dirPath.isEmpty())
    return;

  QDir dir(dirPath);
  const QStringList files = dir.entryList(audioFileNameFilters(), QDir::Files);

  QProgressDialog progressDialog("Loading songs from folder...", QString{}, 0,
                                 static_cast<int>(files.size()), this);
  progressDialog.setWindowModality(Qt::WindowModal);
  progressDialog.setCancelButton(nullptr);
  progressDialog.setMinimumDuration(0);
  progressDialog.setValue(0);

  std::vector<std::string> paths;
  paths.reserve(static_cast<size_t>(files.size()));
  for (const QString &s : files) {
    paths.push_back(dir.absoluteFilePath(s).toStdString());
  }
  playlistTabs->currentPlaylist()->addFiles(paths, [&](int current, int) {
    progressDialog.setValue(current);
    QApplication::processEvents();
  });
  progressDialog.setValue(static_cast<int>(files.size()));
}

void MainWindow::openLibrarySearchDialog() {
  LibrarySearchDialog *dialog =
      new LibrarySearchDialog(songLibrary, columnLayoutManager_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &LibrarySearchDialog::createPlaylistRequested, playlistTabs,
          &PlaylistTabs::createNewPlaylistTabFromSongIds);
  dialog->show();
}

MainWindow::~MainWindow() { delete ui; }

void MainWindow::triggerManualCloudRebase() {
  if (!playlistReady_) {
    return;
  }
  cloudPlayStatsSyncCoordinator_.triggerManualRebase();
}

void MainWindow::refreshManualCloudRebaseActionEnabled() {
  ui->actionManual_cloud_rebase->setEnabled(
      playlistReady_ && cloudPlayStatsSyncCoordinator_.hasValidUuid());
}

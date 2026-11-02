#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "databasemanager.h"
#include "gstaudioplayer.h"
#include "librarysearchdialog.h"
#include "lyricsloader.h"
#include "playbackbuttonstyle.h"
#include "settingsdialog.h"
#include "songparser.h"
#include "systemmediainterfacefactory.h"
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QKeySequence>
#include <QMessageBox>
#include <QProgressDialog>
#include <QRandomGenerator>
#include <QResource>
#include <QSettings>
#include <QSize>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
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

void prewarmSelectedPlaybackBackend() {
  QSettings settings;
  const int backendIndex =
      settings
          .value(
              "playback/backend",
              static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer))
          .toInt();
  if (static_cast<PlaybackBackendManager::Backend>(backendIndex) ==
      PlaybackBackendManager::Backend::GStreamer) {
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

QPixmap scaledCoverPixmap(const QPixmap &source, const QSize &logicalMaxSize,
                          qreal devicePixelRatio) {
  if (source.isNull() || logicalMaxSize.isEmpty()) {
    return {};
  }

  const qreal renderScale = std::max<qreal>(1.0, devicePixelRatio);
  const QSize physicalMaxSize = logicalMaxSize * renderScale;
  const QSize physicalTargetSize =
      source.size().scaled(physicalMaxSize, Qt::KeepAspectRatio);
  QPixmap scaled = source.scaled(physicalTargetSize, Qt::KeepAspectRatio,
                                 Qt::SmoothTransformation);
  scaled.setDevicePixelRatio(renderScale);
  return scaled;
}

} // namespace

MainWindow::MainWindow(QWidget *parent)
    : MainWindow(QString{}, QString{}, parent) {}

MainWindow::MainWindow(QString databaseName, QString connectionName,
                       QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), control{playbackQueue_},
      columnLayoutManager_(columnRegistry_, this),
      databaseManager_(columnRegistry_, std::move(databaseName),
                       std::move(connectionName)),
      songLibrary(columnRegistry_, databaseManager_), lyricsManager{this},
      nowPlayingDisplay_(columnRegistry_),
      cloudPlayStatsSyncCoordinator_(songLibrary, this) {
  prewarmSelectedPlaybackBackend();
  Q_INIT_RESOURCE(resources);
  ui->setupUi(this);
  playlists_ = new PlaylistCollection(songLibrary, databaseManager_, this);
  applyDisplayThemeFromSettings();
  setUpPlaybackButtonIcons(ui);
  defaultWindowTitle_ = windowTitle();
  updatePlaybackTimeStatus();
  setUpMenuBar();
  initCloudSync();
  setUpPlaylist();
  initSettings();
  initPlaybackBackend();
  initVolumeControl();
  setUpPlaybackActions();
  setUpLyricsPanel();
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
  connect(sysMedia, &ISystemMediaInterface::playRequested, this,
          &MainWindow::play);
  connect(sysMedia, &ISystemMediaInterface::pauseRequested, this,
          &MainWindow::pause);
  connect(sysMedia, &ISystemMediaInterface::toggleRequested, this,
          &MainWindow::toggle);
  connect(sysMedia, &ISystemMediaInterface::nextRequested, this,
          &MainWindow::next);
  connect(sysMedia, &ISystemMediaInterface::previousRequested, this,
          &MainWindow::prev);
  connect(sysMedia, &ISystemMediaInterface::seekRequested, this,
          [this](qint64 positionMs) { seek(positionMs); });
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
  connect(playlists_, &PlaylistCollection::playlistAboutToBeRemoved, this,
          [this](int playlistId) {
            const Playlist *removed = playlists_->playlist(playlistId);
            // Removing the playing playlist stops playback; otherwise the
            // queue would drop the current song while audio keeps playing.
            if (playbackQueue_.getCurrentPk().second == removed) {
              stop();
            }
            playbackQueue_.removePlaylist(removed);
          });
  playlistTabs->init(&songLibrary, &playbackQueue_, &control,
                     playbackOrderMenuActionGroup, &columnRegistry_,
                     &columnLayoutManager_, playlists_);
  connect(playlistTabs, &PlaylistTabs::doubleClicked, [this](QModelIndex i) {
    MSong song = control.playIndex(i.row());
    playSong(song, i.row(), playlistTabs->currentPlaylist());
  });
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
              if (backendManager->currentBackend() != backend) {
                backendManager->setBackend(backend);
                control.stop();
                setUpPlaybackBackend();
                applyVolumeToCurrentBackend();
              }
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
              setLyricsPanelFont(fontFamily, pointSize);
            });
    connect(
        dialog, &SettingsDialog::lyricsHighlightColorChanged, this,
        [this](const QColor &color) { setLyricsPanelHighlightColor(color); });
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

void MainWindow::initPlaybackBackend() {
  QSettings settings;
  int backendIndex =
      settings
          .value(
              "playback/backend",
              static_cast<int>(PlaybackBackendManager::Backend::QMediaPlayer))
          .toInt();
  auto backend = static_cast<PlaybackBackendManager::Backend>(backendIndex);
  backendManager = new PlaybackBackendManager(backend, this);
  setUpPlaybackBackend();
}

void MainWindow::setUpPlaybackActions() {
  connect(ui->actionPlay, &QAction::triggered, this, &MainWindow::play);
  connect(ui->play_button, &QAbstractButton::clicked, this, &MainWindow::play);
  connect(ui->actionPause, &QAction::triggered, this, &MainWindow::pause);
  connect(ui->pause_button, &QAbstractButton::clicked, this,
          &MainWindow::pause);
  connect(ui->actionStop, &QAction::triggered, this, &MainWindow::stop);
  connect(ui->actionNext, &QAction::triggered, this, &MainWindow::next);
  connect(ui->actionPrevious, &QAction::triggered, this, &MainWindow::prev);
  connect(ui->stop_button, &QAbstractButton::clicked, this, &MainWindow::stop);
  connect(ui->next_button, &QAbstractButton::clicked, this, &MainWindow::next);
  connect(ui->prev_button, &QAbstractButton::clicked, this, &MainWindow::prev);
  connect(ui->random_button, &QAbstractButton::clicked, this,
          &MainWindow::playRandom);
  skipFailedSongTimer_.setSingleShot(true);
  skipFailedSongTimer_.setInterval(0);
  connect(&skipFailedSongTimer_, &QTimer::timeout, this, &MainWindow::next);
  connect(ui->horizontalSlider, &QSlider::sliderReleased, this,
          [this]() { seek(ui->horizontalSlider->value()); });
}

void MainWindow::setUpLyricsPanel() {
  applyLyricsFontFromSettings();
  applyLyricsHighlightColorFromSettings();
  connect(backendManager->player(), &AudioPlayer::positionChanged,
          &lyricsManager, &LyricsManager::onPlayerProgressChange);
  connect(&lyricsManager, &LyricsManager::newLyricsLineIndex, ui->lyricsPanel,
          &LyricsPanel::updateLyricsPanel);
}

void MainWindow::setLyricsPanelFont(const QString &fontFamily, int pointSize) {
  // Start from the application font so its CJK fallback families are kept.
  QFont font = QApplication::font();
  font.setPointSize(pointSize);
  if (!fontFamily.isEmpty()) {
    QStringList families = font.families();
    families.removeAll(fontFamily);
    families.prepend(fontFamily);
    font.setFamilies(families);
  }
  ui->lyricsPanel->setLyricsTextFont(font);
}

void MainWindow::setLyricsPanelHighlightColor(const QColor &color) {
  ui->lyricsPanel->setHighlightTextColor(color);
}

void MainWindow::applyLyricsFontFromSettings() {
  QSettings settings;
  const QString fontFamily =
      settings.value("lyrics/font_family", QString()).toString();
  const bool useSystemDefaultFont =
      settings.value("lyrics/use_system_default_font", fontFamily.isEmpty())
          .toBool();
  const int defaultLyricsFontSize = QApplication::font().pointSize();
  const int fontSize =
      settings.value("lyrics/font_size", defaultLyricsFontSize).toInt();
  if (useSystemDefaultFont || fontFamily.isEmpty()) {
    setLyricsPanelFont(QString(), fontSize);
    return;
  }
  setLyricsPanelFont(fontFamily, fontSize);
}

void MainWindow::applyLyricsHighlightColorFromSettings() {
  QSettings settings;
  QColor color(
      settings.value("lyrics/highlight_color", QString("#0064ff")).toString());
  if (!color.isValid()) {
    color = QColor(0, 100, 255);
  }
  setLyricsPanelHighlightColor(color);
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
  connect(ui->splitter, &QSplitter::splitterMoved, this,
          &MainWindow::updateImageSize);
  connect(ui->splitter_2, &QSplitter::splitterMoved, this,
          &MainWindow::updateImageSize);
}

void MainWindow::setUpPlaybackBackend() {
  connect(backendManager->player(), &AudioPlayer::mediaStatusChanged, this,
          &MainWindow::statusChanged);
  connect(backendManager->player(), &AudioPlayer::durationChanged, this,
          &MainWindow::durationChanged);
  connect(backendManager->player(), &AudioPlayer::positionChanged, this,
          &MainWindow::positionChanged);
  connect(backendManager->player(), &AudioPlayer::bitrateChanged, this,
          &MainWindow::bitrateChanged);
}

void MainWindow::initVolumeControl() {
  QSettings settings;
  const int savedVolume = clampVolumePercent(
      settings.value(kVolumeSettingsKey, kDefaultVolumePercent).toInt());
  ui->volumeSlider->setValue(savedVolume);
  applyVolumeToCurrentBackend();

  connect(ui->volumeSlider, &QSlider::valueChanged, this, [this](int volume) {
    const int clampedVolume = clampVolumePercent(volume);
    QSettings settings;
    settings.setValue(kVolumeSettingsKey, clampedVolume);
    if (backendManager && backendManager->player()) {
      backendManager->player()->setVolume(clampedVolume);
    }
  });
}

void MainWindow::applyVolumeToCurrentBackend() {
  if (!backendManager || !backendManager->player()) {
    return;
  }
  backendManager->player()->setVolume(
      clampVolumePercent(ui->volumeSlider->value()));
}

void MainWindow::playSong(const MSong &song, int row, Playlist *pl) {
  const int songPk = pl->getPkByIndex(row);
  const std::string filepath = song.at("filepath").text;
  if (filepath.empty()) {
    qFatal("playSong: filepath is empty");
  }
  const QString filepathText = QString::fromStdString(filepath);
  if (!QFileInfo::exists(filepathText)) {
    handlePlaybackFailure(filepathText);
    return;
  }
  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &activeSong =
      songLibrary.refreshSongFromFile(filepath, &remainingFields);
  nowPlayingDisplay_.startSong(activeSong);
  // Set before setSource: InvalidMedia handling reads it (see statusChanged).
  currentTrackPk_ = songPk;
  playCompletionTracker_.startSession();
  songLibrary.markSongPlayedAtStart(songPk, unixNowSeconds());
  playlists_->notifySongChanged(songPk);

  backendManager->player()->setSource(
      QString::fromStdString(activeSong.at("filepath").text));
  backendManager->player()->play();
  sysMedia->setTitleAndArtist(
      QString::fromStdString(activeSong.at("title").text),
      QString::fromStdString(activeSong.at("artist").text));
  setUpImageAndLyrics(activeSong, remainingFields);
  control.play();
  updatePlaybackTimeStatus();
}

void MainWindow::handlePlaybackFailure(const QString &filepath) {
  qWarning() << "Cannot play file:" << filepath;
  // Like switching songs, the previous song stops right away. Otherwise its
  // position updates would reset the failure count and its EndOfMedia could
  // start another skip.
  backendManager->player()->stop();
  ++consecutivePlaybackFailures_;
  const Playlist *pl = playlistTabs->currentPlaylist();
  const int maxFailures = std::max(1, pl != nullptr ? pl->songCount() : 0);
  if (consecutivePlaybackFailures_ < maxFailures) {
    skipFailedSongTimer_.start();
    return;
  }
  stop();
  statusBar()->showMessage(
      tr("Cannot play file: %1").arg(QDir::toNativeSeparators(filepath)));
}

void MainWindow::next() {
  if (!playlistReady_) {
    return;
  }

  const int preferredRow = ui->actionPlayback_follows_cursor->isChecked()
                               ? playlistTabs->firstSelectedRow()
                               : -1;
  const auto &[song, row, pl] = control.next(preferredRow);
  // Nothing to play (the current playlist is empty): stop instead of leaving
  // the previous song playing with no current song.
  if (row < 0) {
    stop();
    return;
  }

  playSong(song, row, pl);
  navigateIndex(row, pl);
}

void MainWindow::prev() {
  if (!playlistReady_) {
    return;
  }
  const auto &[song, row, pl] = control.prev();
  if (row < 0)
    return;

  playSong(song, row, pl);
  navigateIndex(row, pl);
}

void MainWindow::playRandom() {
  if (!playlistReady_) {
    return;
  }

  Playlist *pl = playlistTabs->currentPlaylist();
  if (pl == nullptr || pl->empty()) {
    return;
  }

  const int row = QRandomGenerator::global()->bounded(pl->songCount());
  MSong song = control.playIndex(row);
  playSong(song, row, pl);
  navigateIndex(row, pl);
}

void MainWindow::play() {
  if (!playlistReady_) {
    return;
  }
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    Playlist *pl = playlistTabs->currentPlaylist();
    if (pl->empty()) {
      return;
    }
    // Resume the last played song, or start from the top if it is not in
    // this playlist (new playlist, or the song was removed).
    const int lastPlayedPk = pl->getLastPlayed();
    const int row =
        pl->containsPk(lastPlayedPk) ? pl->getIndexByPk(lastPlayedPk) : 0;
    MSong song = control.playIndex(row);
    playSong(song, row, pl);
  } else {
    backendManager->player()->play();
    control.play();
    updatePlaybackTimeStatus();
    sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Playing);
  }
}

void MainWindow::pause() {
  if (!playlistReady_) {
    return;
  }
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None)
    return;
  backendManager->player()->pause();
  control.pause();
  updatePlaybackTimeStatus();
  sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Paused);
}

void MainWindow::toggle() {
  if (!playlistReady_) {
    return;
  }
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::Playing)
    pause();
  else
    play();
}

void MainWindow::stop() {
  if (!playlistReady_) {
    return;
  }
  skipFailedSongTimer_.stop();
  consecutivePlaybackFailures_ = 0;
  backendManager->player()->stop();
  control.stop();
  nowPlayingDisplay_.stopSong();
  clearImageAndLyrics();
  updatePlaybackTimeStatus();
  sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Stopped);
}

void MainWindow::navigateIndex(int row, Playlist *pl) {
  if (!ui->actionCursor_follows_playback->isChecked()) {
    return;
  }
  playlistTabs->navigateIndex(row, pl);
}

void MainWindow::setUpImageAndLyrics(
    const MSong &song,
    const std::unordered_map<std::string, std::string> &remainingFields) {
  std::string lyricsText;
  for (const char *key : {"attr:unsynced_lyrics", "attr:lyrics"}) {
    auto it = song.find(key);
    if (it != song.end() && !it->second.text.empty()) {
      lyricsText = it->second.text;
      break;
    }
  }
  if (lyricsText.empty()) {
    for (const char *key : {"unsynced_lyrics", "lyrics"}) {
      auto it = remainingFields.find(key);
      if (it != remainingFields.end() && !it->second.empty()) {
        lyricsText = it->second;
        break;
      }
    }
  }
  std::map<int, std::string> lyrics;
  if (!lyricsText.empty()) {
    lyrics = LyricsLoader::parseLyricsText(lyricsText);
  }
  if (lyrics.empty()) {
    lyrics =
        LyricsLoader::getLyrics(song.at("title").text, song.at("artist").text);
  }
  lyricsManager.setLyrics(std::move(lyrics));
  auto [data, size] = SongParser::extractCoverImage(song.at("filepath").text);
  if (size > 0) {
    pixmap.loadFromData(data.data(), size);
    ui->label->clear();
    updateImageSize();
    sysMedia->setArtwork(QByteArray(reinterpret_cast<const char *>(data.data()),
                                    static_cast<int>(size)));
  } else {
    pixmap = QPixmap();
    ui->label->clear();
    ui->label->setText("No cover");
  }
  ui->lyricsPanel->setLyricsPanel(lyricsManager.getCurrentLyricsMap());
}

void MainWindow::clearImageAndLyrics() {
  lyricsManager.setLyrics({});
  ui->lyricsPanel->setLyricsPanel(lyricsManager.getCurrentLyricsMap());
  pixmap = QPixmap();
  ui->label->clear();
}

bool MainWindow::event(QEvent *event) {
  if (event->type() == QEvent::StatusTip) {
    return true;
  }
  return QMainWindow::event(event);
}

void MainWindow::resizeEvent(QResizeEvent *event) {
  QMainWindow::resizeEvent(event);
  updateImageSize();
}

void MainWindow::updateImageSize() {
  if (pixmap.isNull()) {
    return;
  }

  const QSize logicalMaxSize = ui->label->contentsRect().size();
  const qreal devicePixelRatio = ui->label->devicePixelRatioF();
  ui->label->setPixmap(
      scaledCoverPixmap(pixmap, logicalMaxSize, devicePixelRatio));
}

void MainWindow::durationChanged(qint64 newDuration) {
  nowPlayingDisplay_.setDurationMs(newDuration);
  playCompletionTracker_.setDurationMs(newDuration);
  ui->horizontalSlider->setMaximum(newDuration);
  updatePlaybackTimeStatus();
  sysMedia->setDuration(newDuration);
}

void MainWindow::positionChanged(qint64 progress) {
  nowPlayingDisplay_.setPositionMs(progress);
  // Playback is progressing, so the current song is playable.
  if (progress > 0) {
    consecutivePlaybackFailures_ = 0;
  }
  playCompletionTracker_.recordPositionMs(
      progress, control.getStatus() == PlaybackQueue::PlaybackStatus::Playing);
  maybeCountCompletedPlay();

  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
  updatePlaybackTimeStatus();
  sysMedia->updateCurrentPosition(progress);
}

void MainWindow::bitrateChanged(qint64 bitsPerSecond) {
  if (nowPlayingDisplay_.setStreamBitrateBps(bitsPerSecond)) {
    updatePlaybackTimeStatus();
  }
}

const MSong *MainWindow::currentSong() const {
  if (currentTrackPk_ < 0) {
    return nullptr;
  }
  return &songLibrary.getSongByPK(currentTrackPk_);
}

void MainWindow::updateSettingsDisplayPreviewContext(SettingsDialog *dialog) {
  if (!dialog) {
    return;
  }
  dialog->setDisplayExpressionPreviewContext(
      currentSong(), nowPlayingDisplay_.runtimeSymbols(control.getStatus()));
}

void MainWindow::updatePlaybackTimeStatus() {
  const PlaybackQueue::PlaybackStatus status = control.getStatus();
  const MSong *song = currentSong();
  updateSettingsDisplayPreviewContext(findChild<SettingsDialog *>());
  statusBar()->showMessage(nowPlayingDisplay_.statusBarText(status, song));
  setWindowTitle(
      nowPlayingDisplay_.windowTitle(status, song, defaultWindowTitle_));
}

void MainWindow::seek(int mseconds) {
  if (!playlistReady_) {
    return;
  }
  playCompletionTracker_.seeked();
  sysMedia->setPosition(mseconds);
  backendManager->player()->setPosition(mseconds);
}

void MainWindow::statusChanged(QMediaPlayer::MediaStatus status) {
  // TODO
  // handleCursor(status);
  switch (status) {
  case QMediaPlayer::NoMedia:
    break;
  case QMediaPlayer::LoadedMedia:
    break;
  case QMediaPlayer::LoadingMedia:
    break;
  case QMediaPlayer::BufferingMedia:
    break;
  case QMediaPlayer::BufferedMedia:
    break;
  case QMediaPlayer::StalledMedia:
    break;
  case QMediaPlayer::EndOfMedia:
    maybeCountCompletedPlay();
    QApplication::alert(this);
    this->next();
    break;
  case QMediaPlayer::InvalidMedia:
    // InvalidMedia does not say which source failed. currentTrackPk_ is the
    // song passed to setSource: playSong sets it before calling setSource,
    // so keep that order.
    if (control.getStatus() != PlaybackQueue::PlaybackStatus::None &&
        currentTrackPk_ >= 0) {
      handlePlaybackFailure(QString::fromStdString(
          songLibrary.getSongByPK(currentTrackPk_).at("filepath").text));
    }
    break;
  }
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

void MainWindow::maybeCountCompletedPlay() {
  if (currentTrackPk_ < 0 || !playCompletionTracker_.shouldCount()) {
    return;
  }
  if (!songLibrary.incrementPlayCount(currentTrackPk_)) {
    return;
  }
  playCompletionTracker_.markCounted();
  playlists_->notifySongChanged(currentTrackPk_);
  cloudPlayStatsSyncCoordinator_.pushIncrementForSongPk(currentTrackPk_);
}

qint64 MainWindow::unixNowSeconds() {
  return QDateTime::currentSecsSinceEpoch();
}

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

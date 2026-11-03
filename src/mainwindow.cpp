#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "databasemanager.h"
#include "displaytheme.h"
#include "gstaudioplayer.h"
#include "libraryloader.h"
#include "librarysearchdialog.h"
#include "nowplayingpresenter.h"
#include "playbackbuttonstyle.h"
#include "playlistdialogs.h"
#include "playlistsearchdialog.h"
#include "settingsdialog.h"
#include "systemmediainterfacefactory.h"
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QKeySequence>
#include <QLabel>
#include <QResource>
#include <QSettings>
#include <QStatusBar>
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

int clampVolumePercent(int volumePercent) {
  return std::clamp(volumePercent, kMinVolumePercent, kMaxVolumePercent);
}

// Makes action checkable, checks it from the bool setting at key, and saves
// it there when toggled.
void bindActionToBoolSetting(QAction *action, const char *key,
                             bool defaultValue) {
  QSettings settings;
  action->setCheckable(true);
  action->setChecked(settings.value(key, defaultValue).toBool());
  QObject::connect(action, &QAction::toggled, action, [key](bool checked) {
    QSettings settings;
    settings.setValue(key, checked);
  });
}

void prewarmSelectedPlaybackBackend() {
  if (loadPlaybackBackendSetting() ==
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
      monitoredLibrary_(songLibrary, columnRegistry_, databaseManager_, {},
                        &fileSystemMonitor_),
      cloudPlayStatsSyncCoordinator_(songLibrary, this) {
  prewarmSelectedPlaybackBackend();
  Q_INIT_RESOURCE(resources);
  ui->setupUi(this);
  playlists_ = new PlaylistCollection(songLibrary, databaseManager_,
                                      monitoredLibrary_, this);
  playback_ = new PlaybackController(songLibrary, *playlists_, this);
  coverArt_ = new CoverArtPresenter(ui->label, this);
  lyrics_ = new LyricsPresenter(ui->lyricsPanel, this);
  systemMedia_ = new SystemMediaPresenter(*createSystemMediaInterface(this),
                                          *playback_, this);
  connect(coverArt_, &CoverArtPresenter::artworkChanged, systemMedia_,
          &SystemMediaPresenter::setArtwork);
  nowPlaying_ = new NowPlayingPresenter(columnRegistry_, *playback_, *this);
  connect(nowPlaying_, &NowPlayingPresenter::refreshed, this, [this]() {
    updateSettingsDisplayPreviewContext(findChild<SettingsDialog *>());
  });
  applyDisplayThemeMode(loadDisplayThemeModeSetting());
  setUpPlaybackButtonIcons(ui);
  setUpMenuBar();
  initCloudSync();
  setUpPlaylist();
  setUpLibraryScanStatus();
  initSettings();
  setUpPlayback();
  initVolumeControl();
  setUpPlaybackActions();
  lyrics_->applySettings();
  setUpSplitter();
}

void MainWindow::initCloudSync() {
  connect(&cloudPlayStatsSyncCoordinator_,
          &CloudPlayStatsSyncCoordinator::songDataChanged, this,
          [this](int songPk) { playlists_->notifySongChanged(songPk); });
  refreshManualCloudRebaseActionEnabled();
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

  connect(playbackOrderMenuActionGroup, &QActionGroup::triggered, this,
          [](QAction *action) {
            QSettings settings;
            settings.setValue("playback/order_action", action->objectName());
          });

  bindActionToBoolSetting(ui->actionPlayback_follows_cursor,
                          kPlaybackFollowsCursorSettingsKey, false);
  bindActionToBoolSetting(ui->actionCursor_follows_playback,
                          kCursorFollowsPlaybackSettingsKey, true);

  connect(ui->actionSearch, &QAction::triggered, this,
          &MainWindow::openLibrarySearchDialog);
  ui->actionSearch_playlist->setShortcut(QKeySequence::Find);
  connect(ui->actionSearch_playlist, &QAction::triggered, this,
          &MainWindow::openPlaylistSearchDialog);
  connect(ui->actionManual_cloud_rebase, &QAction::triggered, this,
          &MainWindow::triggerManualCloudRebase);
}

void MainWindow::setUpPlaylist() {
  playlistTabs = ui->playlistTabs;
  setPlaylistDependentActionsEnabled(false);
  statusBar()->showMessage(tr("Loading library..."));
  loadLibrary(columnRegistry_, databaseManager_, songLibrary, this,
              [this]() { finishSetUpPlaylist(); });
}

void MainWindow::finishSetUpPlaylist() {
  // The expressions were parsed in the constructor, before loadLibrary loaded
  // the custom columns, so their custom fields were unresolved.
  nowPlaying_->reloadExpressions();
  columnLayoutManager_.refreshFromRegistry();
  monitoredLibrary_.load();
  playlists_->loadAll();
  playlistTabs->init(&songLibrary, &playback_->queue(), &playback_->manager(),
                     playbackOrderMenuActionGroup, &columnRegistry_,
                     &columnLayoutManager_, playlists_);
  connect(playlistTabs, &PlaylistTabs::doubleClicked, playback_,
          [this](QModelIndex i) { playback_->playRow(i.row()); });
  // playlist operations
  connect(ui->actionOpen, &QAction::triggered, this, [this]() {
    addFilesFromDialog(this, *playlistTabs->currentPlaylist());
  });
  connect(ui->actionAdd_folder, &QAction::triggered, this, [this]() {
    addFolderFromDialog(this, *playlistTabs->currentPlaylist());
  });
  connect(ui->actionClear_Playlist, &QAction::triggered, playlistTabs,
          &PlaylistTabs::clearCurrentPlaylist);
  connect(ui->actionNew_autoplaylist, &QAction::triggered, playlistTabs,
          [this]() { playlistTabs->newAutoPlaylist(); });
  connect(playlistTabs, &PlaylistTabs::currentPlaylistChanged, this,
          &MainWindow::updatePlaylistWriteActionsEnabled);
  playlistReady_ = true;
  playback_->setReady(true);
  setPlaylistDependentActionsEnabled(true);
  // Playback is available before the scan; it runs in the background.
  monitoredLibrary_.rescan();
  cloudPlayStatsSyncCoordinator_.startSync();
}

void MainWindow::setPlaylistDependentActionsEnabled(bool enabled) {
  updatePlaylistWriteActionsEnabled();
  ui->actionNew_autoplaylist->setEnabled(enabled);
  ui->actionSearch->setEnabled(enabled);
  ui->actionSearch_playlist->setEnabled(enabled);
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

void MainWindow::updatePlaylistWriteActionsEnabled() {
  const Playlist *shown =
      playlistReady_ ? playlistTabs->currentPlaylist() : nullptr;
  const bool manual =
      shown != nullptr && shown->kind() == Playlist::Kind::Manual;
  ui->actionOpen->setEnabled(manual);
  ui->actionAdd_folder->setEnabled(manual);
  ui->actionClear_Playlist->setEnabled(manual);
}

void MainWindow::setUpLibraryScanStatus() {
  auto *label = new QLabel(this);
  label->setObjectName("library_scan_status_label");
  label->hide();
  statusBar()->addPermanentWidget(label);
  connect(&monitoredLibrary_, &MonitoredLibrary::scanProgress, label,
          [label](int done, int total) {
            label->setText(
                tr("Scanning library... %1 / %2").arg(done).arg(total));
            label->show();
          });
  connect(&monitoredLibrary_, &MonitoredLibrary::scanFinished, label,
          [this, label]() {
            const std::vector<MonitoredLibrary::Folder> folders =
                monitoredLibrary_.folders();
            const auto offline =
                std::count_if(folders.begin(), folders.end(),
                              [](const MonitoredLibrary::Folder &folder) {
                                return !folder.online;
                              });
            label->setText(offline == 1
                               ? tr("1 library folder offline")
                               : tr("%1 library folders offline").arg(offline));
            label->setVisible(offline > 0);
          });
}

void MainWindow::initSettings() {
  connect(ui->actionPreferences, &QAction::triggered, this, [this]() {
    SettingsDialog *dialog = new SettingsDialog(
        columnRegistry_, databaseManager_, monitoredLibrary_, this);
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
      nowPlaying_->reloadExpressions();
      columnLayoutManager_.refreshFromRegistry();
      playlists_->reloadAutoPlaylistExpressions();
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
              nowPlaying_->setExpression(DisplayExpressionKind::StatusBar,
                                         expression);
            });
    connect(dialog, &SettingsDialog::windowTitleExpressionChanged, this,
            [this](const QString &expression) {
              nowPlaying_->setExpression(DisplayExpressionKind::WindowTitle,
                                         expression);
            });
    connect(dialog, &SettingsDialog::displayThemeModeChanged, this,
            [this](const QString &mode) {
              applyDisplayThemeMode(mode);
              setUpPlaybackButtonIcons(ui);
            });
    updateSettingsDisplayPreviewContext(dialog);
    dialog->show();
  });
}

void MainWindow::setUpPlayback() {
  // Created after setUpPlaylist, so a file-backed library loads on its worker
  // thread meanwhile.
  playback_->setBackend(loadPlaybackBackendSetting());
  playback_->setShownPlaylist(
      [this]() { return playlistTabs->currentPlaylist(); });
  playback_->setPreferredNextRow([this]() {
    return ui->actionPlayback_follows_cursor->isChecked()
               ? playlistTabs->firstSelectedRow()
               : -1;
  });
  connect(
      playback_, &PlaybackController::songStarted, this,
      [this](
          const MSong &song,
          const std::unordered_map<std::string, std::string> &remainingFields) {
        systemMedia_->showSong(song);
        lyrics_->showSong(song, remainingFields);
        // After systemMedia_->showSong, which clears the system artwork.
        coverArt_->showSong(song);
      });
  connect(playback_, &PlaybackController::playbackStateChanged, this,
          &MainWindow::playbackStateChanged);
  connect(playback_, &PlaybackController::movedToRow, this,
          &MainWindow::navigateIndex);
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
  if (status == PlaybackQueue::PlaybackStatus::None) {
    lyrics_->clear();
    coverArt_->clear();
  }
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
  ui->horizontalSlider->setMaximum(newDuration);
}

void MainWindow::positionChanged(qint64 progress) {
  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
  lyrics_->setPositionMs(progress);
}

void MainWindow::updateSettingsDisplayPreviewContext(SettingsDialog *dialog) {
  if (!dialog) {
    return;
  }
  dialog->setDisplayExpressionPreviewContext(playback_->currentSong(),
                                             nowPlaying_->runtimeSymbols());
}

void MainWindow::openLibrarySearchDialog() {
  LibrarySearchDialog *dialog = new LibrarySearchDialog(
      songLibrary, monitoredLibrary_, columnLayoutManager_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(dialog, &LibrarySearchDialog::createPlaylistRequested, playlistTabs,
          &PlaylistTabs::createNewPlaylistTabFromSongIds);
  connect(dialog, &LibrarySearchDialog::saveAsAutoPlaylistRequested,
          playlistTabs, &PlaylistTabs::newAutoPlaylist);
  dialog->show();
}

void MainWindow::openPlaylistSearchDialog() {
  if (playlistSearchDialog_) {
    playlistSearchDialog_->raise();
    playlistSearchDialog_->activateWindow();
    return;
  }
  auto *dialog = new PlaylistSearchDialog(songLibrary, *playlists_,
                                          columnLayoutManager_, this);
  dialog->setAttribute(Qt::WA_DeleteOnClose);
  dialog->setPlaylist(playlistTabs->currentPlaylist());
  connect(playlistTabs, &PlaylistTabs::currentPlaylistChanged, dialog,
          &PlaylistSearchDialog::setPlaylist);
  // The rows are rows of the shown playlist, which the dialog searches.
  connect(dialog, &PlaylistSearchDialog::playRequested, this, [this](int row) {
    playback_->playRow(row);
    playlistTabs->navigateIndex(row, playlistTabs->currentPlaylist());
  });
  connect(dialog, &PlaylistSearchDialog::showRequested, this, [this](int row) {
    playlistTabs->navigateIndex(row, playlistTabs->currentPlaylist());
  });
  connect(dialog, &PlaylistSearchDialog::queueRequested, this,
          [this](int row) { playback_->manager().queueEnd(row); });
  connect(dialog, &PlaylistSearchDialog::propertiesRequested, playlistTabs,
          &PlaylistTabs::openPropertiesForRow);
  playlistSearchDialog_ = dialog;
  dialog->show();
}

MainWindow::~MainWindow() {
  // It uses columnLayoutManager_, which is destroyed before the window's
  // children.
  delete playlistSearchDialog_;
  delete ui;
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

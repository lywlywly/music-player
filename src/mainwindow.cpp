#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "databasemanager.h"
#include "displaytheme.h"
#include "gstaudioplayer.h"
#include "imenuprovider.h"
#include "ipanelprovider.h"
#include "layout/panellayouteditor.h"
#include "layout/panellayoutview.h"
#include "libraryloader.h"
#include "librarysearchdialog.h"
#include "mainwindowlayout.h"
#include "nowplayingpresenter.h"
#include "playbackbuttonstyle.h"
#include "playlistdialogs.h"
#include "playlistsearchdialog.h"
#include "pluginmanager.h"
#include "replaygain.h"
#include "settingsdialog.h"
#include "songdialogs.h"
#include "songinfopanel.h"
#include "songinfopresenter.h"
#include "systemmediainterfacefactory.h"
#include <QActionGroup>
#include <QApplication>
#include <QDir>
#include <QEvent>
#include <QKeySequence>
#include <QLabel>
#include <QMessageBox>
#include <QResource>
#include <QSettings>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTimer>
#include <algorithm>
#include <utility>
#include <vector>

namespace {
constexpr int kMinVolumePercent = 0;
constexpr int kMaxVolumePercent = 100;
constexpr int kDefaultVolumePercent = 100;
constexpr const char *kVolumeSettingsKey = "playback/volume_percent";
constexpr const char *kPlaybackFollowsCursorSettingsKey =
    "playback/playback_follows_cursor";
constexpr const char *kCursorFollowsPlaybackSettingsKey =
    "playback/cursor_follows_playback";
// The toolbar's and the panel layout's trees, as PanelLayout::toJson
// (docs/layout-editing.md).
constexpr const char *kToolbarLayoutSettingsKey = "layout/toolbar";
constexpr const char *kPanelLayoutSettingsKey = "layout/tree";

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

// The folder the plugins are loaded from: the bundle's PlugIns when the
// executable is in a macOS app bundle, else plugins/ next to the executable.
QString pluginFolder() {
  const QDir exeDir(QCoreApplication::applicationDirPath());
  if (exeDir.absolutePath().endsWith(".app/Contents/MacOS")) {
    return exeDir.absoluteFilePath("../PlugIns");
  }
  return exeDir.absoluteFilePath("plugins");
}

// The folder the plugins' data folders are in: plugins/ beside the app's
// database (docs/plugins/plugin-system.md, "Core: every plugin").
QString defaultPluginDataRoot() {
  return QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation))
      .filePath("plugins");
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
    : MainWindow(QString{}, QString{}, defaultPluginDataRoot(), parent) {}

MainWindow::MainWindow(QString databaseName, QString connectionName,
                       const QString &pluginDataRoot, QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow),
      columnLayoutManager_(columnRegistry_, this),
      databaseManager_(columnRegistry_, std::move(databaseName),
                       std::move(connectionName)),
      songLibrary(columnRegistry_, databaseManager_),
      monitoredLibrary_(songLibrary, columnRegistry_, databaseManager_, {},
                        &fileSystemMonitor_) {
  prewarmSelectedPlaybackBackend();
  Q_INIT_RESOURCE(resources);
  ui->setupUi(this);
  playlists_ = new PlaylistCollection(songLibrary, databaseManager_,
                                      monitoredLibrary_, this);
  playback_ = new PlaybackController(songLibrary, *playlists_, this);
  coverArt_ = new CoverArtPresenter(ui->coverLabel, this);
  // A child created after ui->setupUi, so the plugin panels in the layout are
  // destroyed before it shuts the plugins down.
  plugins_ =
      new PluginManager(*playback_, pluginFolder(), pluginDataRoot, this);
  // Before NowPlayingPresenter parses the display expressions, so the
  // plugins' fields resolve, and before the library loads their values.
  plugins_->registerSongFields(columnRegistry_, songLibrary);
  setUpProvidedFields();
  songInfo_ = new SongInfoPresenter(ui->songInfoPanel, songLibrary,
                                    monitoredLibrary_, columnRegistry_, this);
  connect(ui->songInfoPanel, &SongInfoPanel::revealRequested, this,
          [this](int songPk) {
            revealSongFile(this, songLibrary.getSongByPK(songPk));
          });
  connect(ui->songInfoPanel, &SongInfoPanel::editPropertiesRequested, this,
          [this](int songPk) {
            openSongProperties(this, songPk, songLibrary, columnRegistry_,
                               *playlists_);
          });
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
  setUpLayout();
  setUpPluginMenus();
  setUpPlaylist();
  setUpLibraryScanStatus();
  initSettings();
  setUpPlayback();
  initVolumeControl();
  setUpPlaybackActions();
}

void MainWindow::setUpProvidedFields() {
  connect(plugins_, &PluginManager::providedValuesChanged, this,
          [this](const QString &pluginId, const QStringList &keys) {
            songLibrary.markStale(pluginId, keys);
          });
  // One refresh per batch of stale keys, from the event loop: a scan adding
  // songs in a loop is one read after it, and a plugin's change is not
  // applied inside the call that reported it (docs/plugins/playstats.md,
  // "One path for every change after the load").
  songLibrary.setStaleCallback([this]() {
    QTimer::singleShot(0, this, [this]() {
      playlists_->notifySongsChanged(songLibrary.refreshStale());
    });
  });
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
}

void MainWindow::setUpPluginMenus() {
  // At the end of Playback, in load order, after one separator; none without
  // a provider. Only a Loaded entry has a root object; qobject_cast of the
  // others' null root is null. The menus are destroyed with the menu bar,
  // before the plugins are shut down.
  bool separated = false;
  for (const PluginEntry &entry : plugins_->plugins()) {
    auto *provider = qobject_cast<IMenuProvider *>(entry.root);
    if (provider == nullptr) {
      continue;
    }
    if (!separated) {
      ui->menuPlayback->addSeparator();
      separated = true;
    }
    ui->menuPlayback->addMenu(provider->createMenu(ui->menuPlayback));
  }
}

void MainWindow::setUpLayout() {
  // Each view is a child of its host. Both park their elements in
  // layoutElements; each moves only its own.
  auto *toolbarView =
      new PanelLayoutView(ui->toolbarHost, ui->layoutElements,
                          tr("Empty. Turn on View > Edit Layout, then "
                             "right-click here to insert a control."));
  toolbarView->addElement(MainWindowLayout::kPlaybackButtons,
                          tr("Playback Buttons"), ui->playbackButtons);
  toolbarView->addElement(MainWindowLayout::kSeekBar, tr("Seek Bar"),
                          ui->seekSlider);
  toolbarView->addElement(MainWindowLayout::kVolume, tr("Volume"),
                          ui->volumeSlider);

  auto *panelView =
      new PanelLayoutView(ui->layoutHost, ui->layoutElements,
                          tr("Empty. Turn on View > Edit Layout, then "
                             "right-click here to insert a panel."));
  panelView->addElement(MainWindowLayout::kPlaylist, tr("Playlist"),
                        ui->playlistTabs);
  panelView->addElement(MainWindowLayout::kProperties, tr("Properties"),
                        ui->songInfoPanel);
  panelView->addElement(MainWindowLayout::kCover, tr("Cover Art"),
                        ui->coverLabel);
  // The loaded plugins' panels, in load order, each named by its plugin's id.
  // A plugin not loaded only names its node, if the layout has one. Only a
  // Loaded entry has a root object; qobject_cast of the others' null root is
  // null. The panels are created in layoutElements, part of the UI, so they
  // are destroyed before plugins_ shuts the plugins down.
  for (const PluginEntry &entry : plugins_->plugins()) {
    if (entry.state != PluginEntry::State::Loaded && !entry.id.isEmpty()) {
      panelView->setUnavailableTitle(entry.id, entry.name);
    }
    auto *provider = qobject_cast<IPanelProvider *>(entry.root);
    if (provider == nullptr) {
      continue;
    }
    panelView->addElement(entry.id, provider->panelTitle(),
                          provider->createPanel(ui->layoutElements));
  }

  using Kind = PanelLayout::Kind;
  // The toolbar is one row that stays: its controls are placed, moved and
  // removed in it, and it holds no other container.
  auto *toolbarEditor = new PanelLayoutEditor(*toolbarView, {});
  toolbarEditor->setContainerTitle(Kind::HBox, tr("Toolbar"));
  toolbarEditor->fixRoot();
  const std::function<void()> resetToolbar =
      setUpLayoutView(*toolbarView, *toolbarEditor, kToolbarLayoutSettingsKey,
                      MainWindowLayout::defaultToolbar());
  const std::function<void()> resetPanels = setUpLayoutView(
      *panelView,
      *new PanelLayoutEditor(*panelView,
                             {Kind::HSplitter, Kind::VSplitter, Kind::Tabs}),
      kPanelLayoutSettingsKey, MainWindowLayout::defaultPanels());
  connect(ui->actionReset_layout, &QAction::triggered, this,
          [this, resetToolbar, resetPanels]() {
            if (QMessageBox::question(
                    this, tr("Reset Layout"),
                    tr("Replace the toolbar and the layout with the default "
                       "ones? Your changes to them are lost.")) !=
                QMessageBox::Yes) {
              return;
            }
            resetToolbar();
            resetPanels();
          });
}

std::function<void()>
MainWindow::setUpLayoutView(PanelLayoutView &view, PanelLayoutEditor &editor,
                            const char *key, const PanelLayout &defaultLayout) {
  const QVariant saved = QSettings().value(key);
  std::optional<PanelLayout> layout =
      PanelLayout::fromJson(saved.toString().toUtf8());
  if (saved.isValid() && !layout) {
    qWarning("MainWindow: %s is not a valid layout; using the default", key);
  }
  view.setLayout(layout.value_or(defaultLayout));

  const auto save = [&view, key]() {
    QSettings().setValue(key, QString::fromUtf8(view.layout().toJson()));
  };
  connect(&view, &PanelLayoutView::layoutChanged, this, save);
  connect(&editor, &PanelLayoutEditor::layoutChanged, this, save);
  connect(ui->actionEdit_layout, &QAction::toggled, &editor,
          &PanelLayoutEditor::setEnabled);
  return [&view, defaultLayout, save]() {
    view.setLayout(defaultLayout);
    save();
  };
}

void MainWindow::setUpPlaylist() {
  playlistTabs = ui->playlistTabs;
  setPlaylistDependentActionsEnabled(false);
  statusBar()->showMessage(tr("Loading library..."));
  libraryLoad_ = loadLibrary(columnRegistry_, databaseManager_, songLibrary,
                             this, [this]() { finishSetUpPlaylist(); });
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
  connect(playlistTabs, &PlaylistTabs::selectedSongChanged, songInfo_,
          &SongInfoPresenter::showSong);
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
    SettingsDialog *dialog =
        new SettingsDialog(columnRegistry_, databaseManager_, monitoredLibrary_,
                           plugins_->plugins(), this);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setModal(true);
    connect(dialog, &SettingsDialog::backendChanged, this,
            [this](PlaybackBackendManager::Backend backend) {
              playback_->setBackend(backend);
            });
    connect(dialog, &SettingsDialog::replayGainModeChanged, playback_,
            &PlaybackController::setReplayGainMode);
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
  playback_->setReplayGainMode(loadReplayGainModeSetting());
  playback_->setBackend(loadPlaybackBackendSetting());
  playback_->setShownPlaylist(
      [this]() { return playlistTabs->currentPlaylist(); });
  playback_->setPreferredNextRow([this]() {
    return ui->actionPlayback_follows_cursor->isChecked()
               ? playlistTabs->firstSelectedRow()
               : -1;
  });
  connect(playback_, &PlaybackController::songStarted, this,
          [this](const MSong &song) {
            systemMedia_->showSong(song);
            // After systemMedia_->showSong, which clears the system artwork.
            coverArt_->showSong(song);
          });
  connect(playback_, &PlaybackController::playbackStateChanged, this,
          &MainWindow::playbackStateChanged);
  connect(playback_, &PlaybackController::movedToRow, this,
          &MainWindow::navigateIndex);
  connect(playback_, &PlaybackController::songFinished, this,
          [this]() { QApplication::alert(this); });
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
  connect(ui->seekSlider, &QSlider::sliderReleased, playback_,
          [this]() { playback_->seek(ui->seekSlider->value()); });
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

void MainWindow::durationChanged(qint64 newDuration) {
  ui->seekSlider->setMaximum(newDuration);
}

void MainWindow::positionChanged(qint64 progress) {
  if (!ui->seekSlider->isSliderDown())
    ui->seekSlider->setValue(progress);
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
  // The load's workers call the plugins' song field providers, so they finish
  // before plugins_, a child destroyed after this body, shuts the plugins
  // down (docs/plugins/playstats.md, "Lifetime").
  libraryLoad_.waitForFinished();
  // It uses columnLayoutManager_, which is destroyed before the window's
  // children.
  delete playlistSearchDialog_;
  delete ui;
}

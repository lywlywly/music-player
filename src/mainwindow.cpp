#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#ifdef MYPLAYER_TESTING
#include "dummysystemmediainterface.h"
#endif
#ifdef Q_OS_MACOS
#include "macosmediacenter.h"
#elif defined(Q_OS_LINUX)
#include "mprismediainterface.h"
#elif defined(Q_OS_WIN)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "windowsmediacenter.h"
#endif
#include "databasemanager.h"
#include "displayexpressionevalcontext.h"
#include "gstaudioplayer.h"
#include "libraryexpression_ops.h"
#include "libraryexpression_parser.h"
#include "librarysearchdialog.h"
#include "lyricsloader.h"
#include "settingsdialog.h"
#include "songparser.h"
#include <QActionGroup>
#include <QApplication>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QFutureWatcher>
#include <QIcon>
#include <QPainter>
#include <QProgressDialog>
#include <QResource>
#include <QSettings>
#include <QSize>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QStatusBar>
#include <QStyleHints>
#include <QThread>
#include <QTimer>
#include <QToolButton>
#include <QtConcurrent>
#include <algorithm>
#include <utility>

namespace {
constexpr int kPlaybackIconSize = 18;

ExprSymbolResolver makeDisplayExprResolver(const ColumnRegistry &registry) {
  return ExprSymbolResolver(
      mergeExprSymbols(std::vector<ExprSymbolInfo>(
                           StatusRuntimeSymbolTable::expressionSymbols()),
                       registry.expressionSymbols()));
}

ExprPtr parseDisplayExpressionWithFallback(const ExprSymbolResolver &resolver,
                                           const QString &expression,
                                           const QString &defaultExpression,
                                           const QString &logPrefix) {
  ExprParseResult parsed = parseLibraryExpression(expression, resolver);
  if (!parsed.ok()) {
    qWarning() << logPrefix << "parse failed:" << parsed.error.message << "at"
               << parsed.error.position;
    parsed = parseLibraryExpression(defaultExpression, resolver);
    if (!parsed.ok()) {
      qFatal("%s: failed to parse default expression",
             logPrefix.toUtf8().constData());
    }
  }
  return std::move(parsed.expr);
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

QColor playbackIconColor() {
  if (QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark) {
    return QColor(QStringLiteral("#fcfcfc"));
  }
  return QColor(QStringLiteral("#232629"));
}

QIcon tintedIcon(const QString &iconPath, const QColor &color,
                 qreal iconRenderScale) {
  const QSize iconSize(kPlaybackIconSize, kPlaybackIconSize);
  const qreal renderScale = std::max<qreal>(1.0, iconRenderScale);
  const QRect logicalRect(QPoint(0, 0), iconSize);
  QPixmap pixmap(iconSize * renderScale);
  pixmap.setDevicePixelRatio(renderScale);
  pixmap.fill(Qt::transparent);

  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
  painter.drawPixmap(logicalRect, QIcon(iconPath).pixmap(pixmap.size()));
  painter.setCompositionMode(QPainter::CompositionMode_SourceIn);
  painter.fillRect(logicalRect, color);
  return QIcon(pixmap);
}

void setIconOnlyButton(QToolButton *button, const QString &iconPath,
                       const QColor &color) {
  const QString label =
      button->text().isEmpty() ? button->toolTip() : button->text();
  const qreal iconRenderScale = button->devicePixelRatioF();
  button->setIcon(tintedIcon(iconPath, color, iconRenderScale));
  button->setText(QString());
  button->setToolTip(label);
  button->setAccessibleName(label);
  button->setIconSize(QSize(kPlaybackIconSize, kPlaybackIconSize));
  button->setFixedSize(24, 24);
  button->setAutoRaise(false);
  button->setToolButtonStyle(Qt::ToolButtonIconOnly);
}

void setUpPlaybackButtonIcons(Ui::MainWindow *ui) {
  const QColor color = playbackIconColor();
  setIconOnlyButton(ui->play_button,
                    QStringLiteral(":/icons/media-playback-start.svg"), color);
  setIconOnlyButton(ui->pause_button,
                    QStringLiteral(":/icons/media-playback-pause.svg"), color);
  setIconOnlyButton(ui->stop_button,
                    QStringLiteral(":/icons/media-playback-stop.svg"), color);
  setIconOnlyButton(ui->prev_button,
                    QStringLiteral(":/icons/media-skip-backward.svg"), color);
  setIconOnlyButton(ui->next_button,
                    QStringLiteral(":/icons/media-skip-forward.svg"), color);
  setIconOnlyButton(ui->random_button,
                    QStringLiteral(":/icons/media-playback-random.svg"), color);
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
      cloudPlayStatsSyncCoordinator_(songLibrary, this) {
  prewarmSelectedPlaybackBackend();
  Q_INIT_RESOURCE(resources);
  ui->setupUi(this);
  applyDisplayThemeFromSettings();
  setUpPlaybackButtonIcons(ui);
  defaultWindowTitle_ = windowTitle();
  initStatusBarExpression();
  initWindowTitleExpression();
  updatePlaybackTimeStatus();
  setUpMenuBar();
  initCloudSync();
  setUpPlaylist();
  initSettings();
  initPlaybackBackend();
  setUpPlaybackActions();
  setUpLyricsPanel();
  setUpSplitter();
  setupSystemMediaInterface();
}

void MainWindow::initCloudSync() {
  connect(&cloudPlayStatsSyncCoordinator_,
          &CloudPlayStatsSyncCoordinator::songDataChanged, this,
          [this](int songPk) {
            playlistTabs->notifySongDataChangedInAllPlaylists(songPk);
          });
  refreshManualCloudRebaseActionEnabled();
}

void MainWindow::setupSystemMediaInterface() {
#ifdef MYPLAYER_TESTING
  if (qEnvironmentVariableIntValue("MYPLAYER_USE_DUMMY_MEDIA_INTERFACE") != 0) {
    sysMedia = new DummySystemMediaInterface(this);
  } else
#endif
  {
#ifdef Q_OS_MACOS
    sysMedia = new MacOSMediaCenter(this);
#elif defined(Q_OS_LINUX)
    sysMedia = new MprisMediaInterface(this);
#elif defined(Q_OS_WIN)
    sysMedia = new WindowsMediaCenter(static_cast<quintptr>(winId()), this);
#else
    qFatal("setupSystemMediaInterface: unsupported platform");
#endif
  }

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
  columnLayoutManager_.refreshFromRegistry();
  songLibrary.replaceWithSnapshot(std::move(snapshot));
  playlistTabs->init(&songLibrary, &playbackQueue_, &control,
                     playbackOrderMenuActionGroup, &columnRegistry_,
                     &columnLayoutManager_, &databaseManager_);
  connect(playlistTabs, &PlaylistTabs::doubleClicked, [this](QModelIndex i) {
    MSong song = control.playIndex(i.row());
    playSong(song, i.row(), playlistTabs->currentPlaylist());
  });
  // playlist operations
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionAdd_folder, &QAction::triggered, this,
          &MainWindow::openFolder);
  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
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
  ui->actionAdd->setEnabled(enabled);
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
  connect(ui->actionSettings, &QAction::triggered, this, [this]() {
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
              }
            });
    connect(dialog, &SettingsDialog::customFieldsChanged, this, [this]() {
      if (!columnRegistry_.loadDynamicColumns(databaseManager_.db())) {
        qFatal("initSettings: failed to reload dynamic columns");
      }
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
            [this](const QString &) {
              initStatusBarExpression();
              updatePlaybackTimeStatus();
            });
    connect(dialog, &SettingsDialog::windowTitleExpressionChanged, this,
            [this](const QString &) {
              initWindowTitleExpression();
              updatePlaybackTimeStatus();
            });
    connect(dialog, &SettingsDialog::displayThemeModeChanged, this,
            [this](const QString &mode) { applyDisplayThemeMode(mode); });
    updateStatusRuntimeSymbols();
    const MSong *activeSong = nullptr;
    if (currentTrackPk_ >= 0) {
      activeSong = &songLibrary.getSongByPK(currentTrackPk_);
    }
    dialog->setDisplayExpressionPreviewContext(activeSong,
                                               statusRuntimeSymbols_);
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
  if (fontFamily.isEmpty()) {
    QFont font = QApplication::font();
    font.setPointSize(pointSize);
    ui->lyricsPanel->setLyricsTextFont(font);
    return;
  }
  QFont font(fontFamily, pointSize);
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

void MainWindow::playSong(const MSong &song, int row, Playlist *pl) {
  const int songPk = pl->getPkByIndex(row);
  const std::string filepath = song.at("filepath").text;
  if (filepath.empty()) {
    qFatal("playSong: filepath is empty");
  }
  std::unordered_map<std::string, std::string> remainingFields;
  const MSong &activeSong =
      songLibrary.refreshSongFromFile(filepath, &remainingFields);
  currentBitrateBps_ = 0;
  currentTagBitrateBps_ = 0;
  useTagBitrateForCurrentTrack_ = false;
  if (const auto bitrateIt = activeSong.find("bitrate");
      bitrateIt != activeSong.end()) {
    const FieldValue &bitrate = bitrateIt->second;
    if (bitrate.valueType() == ValueType::Number) {
      currentTagBitrateBps_ = static_cast<qint64>(bitrate.typed.numberDouble *
                                                  static_cast<double>(1000));
    }
  }
  if (currentTagBitrateBps_ > 0) {
    useTagBitrateForCurrentTrack_ = SongParser::isLikelyCbrAudioFile(filepath);
  }
  resetPlayStatsSession(songPk);
  songLibrary.markSongPlayedAtStart(songPk, unixNowSeconds());
  playlistTabs->notifySongDataChangedInAllPlaylists(songPk);

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

void MainWindow::next() {
  if (!playlistReady_) {
    return;
  }
  const auto &[song, row, pl] = control.next();
  if (row < 0)
    return;

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

void MainWindow::play() {
  if (!playlistReady_) {
    return;
  }
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    Playlist *pl = playlistTabs->currentPlaylist();
    int lastPlayedPk = pl->getLastPlayed();
    const int row = pl->getIndexByPk(lastPlayedPk);
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
  backendManager->player()->stop();
  control.stop();
  currentBitrateBps_ = 0;
  currentTagBitrateBps_ = 0;
  useTagBitrateForCurrentTrack_ = false;
  updatePlaybackTimeStatus();
  sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Stopped);
}

void MainWindow::navigateIndex(int row, Playlist *pl) {
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
    ui->label->setPixmap(pixmap.scaled(ui->splitter->sizes().first() - 10,
                                       ui->splitter_2->sizes().last() - 10,
                                       Qt::KeepAspectRatio));
    sysMedia->setArtwork(QByteArray(reinterpret_cast<const char *>(data.data()),
                                    static_cast<int>(size)));
  } else {
    ui->label->setText("No cover");
  }
  ui->lyricsPanel->setLyricsPanel(lyricsManager.getCurrentLyricsMap());
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
  int newSize = ui->splitter->sizes().first() - 10;
  int newHeight = ui->splitter_2->sizes().last() - 10;
  QPixmap scaledPixmap = pixmap.scaled(newSize, newHeight, Qt::KeepAspectRatio);
  ui->label->setPixmap(scaledPixmap);
}

void MainWindow::addEntry() {
  if (!playlistReady_) {
    return;
  }
  AddEntryDialog *addEntryDialog = new AddEntryDialog(this);
  addEntryDialog->show();
  addEntryDialog->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(addEntryDialog, &AddEntryDialog::entryStringEntered,
                   [this](const QString &text) {
                     playlistTabs->currentPlaylist()->addSong(
                         songLibrary.loadSongFromFile(text.toStdString()));
                   });
}

void MainWindow::durationChanged(qint64 newDuration) {
  currentDurationMs_ = newDuration;
  sessionDurationMs_ = newDuration;
  ui->horizontalSlider->setMaximum(newDuration);
  updatePlaybackTimeStatus();
  sysMedia->setDuration(newDuration);
}

void MainWindow::positionChanged(qint64 progress) {
  currentPositionMs_ = progress;
  if (progress > sessionMaxPositionMs_) {
    sessionMaxPositionMs_ = progress;
  }
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::Playing &&
      lastPositionSampleMs_ >= 0) {
    const qint64 delta = progress - lastPositionSampleMs_;
    if (delta > 0) {
      sessionListenedMs_ += delta;
    }
  }
  lastPositionSampleMs_ = progress;
  maybeCountCompletedPlay();

  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
  updatePlaybackTimeStatus();
  sysMedia->updateCurrentPosition(progress);
}

void MainWindow::bitrateChanged(qint64 bitsPerSecond) {
  if (useTagBitrateForCurrentTrack_ && currentTagBitrateBps_ > 0) {
    return;
  }
  currentBitrateBps_ = std::max<qint64>(0, bitsPerSecond);
  updatePlaybackTimeStatus();
}

void MainWindow::initStatusBarExpression() {
  const ExprSymbolResolver resolver = makeDisplayExprResolver(columnRegistry_);
  const QString defaultExpression =
      StatusRuntimeSymbolTable::defaultStatusBarExpression();
  QSettings settings;
  QString expression =
      settings.value("status_bar/expression", defaultExpression)
          .toString()
          .trimmed();
  if (expression.isEmpty()) {
    expression = defaultExpression;
  }
  statusBarExpr_ = parseDisplayExpressionWithFallback(
      resolver, expression, defaultExpression,
      QStringLiteral("initStatusBarExpression"));
}

void MainWindow::initWindowTitleExpression() {
  const ExprSymbolResolver resolver = makeDisplayExprResolver(columnRegistry_);
  const QString defaultExpression =
      StatusRuntimeSymbolTable::defaultWindowTitleExpression();
  QSettings settings;
  QString expression =
      settings.value("window_title/expression", defaultExpression)
          .toString()
          .trimmed();
  if (expression.isEmpty()) {
    expression = defaultExpression;
  }
  windowTitleExpr_ = parseDisplayExpressionWithFallback(
      resolver, expression, defaultExpression,
      QStringLiteral("initWindowTitleExpression"));
}

qint64 MainWindow::effectiveBitrateKbps() const {
  const qint64 tagKbps = currentTagBitrateBps_ / static_cast<qint64>(1000);
  if (useTagBitrateForCurrentTrack_ && tagKbps > 0) {
    return tagKbps;
  }

  const qint64 runtimeKbps = currentBitrateBps_ / static_cast<qint64>(1000);
  if (runtimeKbps > 0) {
    return runtimeKbps;
  }
  return tagKbps > 0 ? tagKbps : 0;
}

void MainWindow::updateStatusRuntimeSymbols() {
  const auto status = control.getStatus();
  statusRuntimeSymbols_.setIsPlaying(status ==
                                     PlaybackQueue::PlaybackStatus::Playing);
  statusRuntimeSymbols_.setIsPaused(status ==
                                    PlaybackQueue::PlaybackStatus::Paused);
  statusRuntimeSymbols_.setPlaybackTimeSeconds(currentPositionMs_ / 1000);
  statusRuntimeSymbols_.setDurationSeconds(currentDurationMs_ / 1000);
  statusRuntimeSymbols_.setBitrateKbps(effectiveBitrateKbps());
}

QString MainWindow::evaluateStatusBarExpression() const {
  if (!statusBarExpr_) {
    return {};
  }
  const MSong *activeSong = nullptr;
  if (currentTrackPk_ >= 0) {
    activeSong = &songLibrary.getSongByPK(currentTrackPk_);
  }
  DisplayExpressionEvalContext context(statusRuntimeSymbols_, activeSong);
  return runtimeValueToQString(statusBarExpr_->evaluateValue(context));
}

QString MainWindow::evaluateWindowTitleExpression() const {
  if (!windowTitleExpr_) {
    return defaultWindowTitle_;
  }
  const MSong *activeSong = nullptr;
  if (currentTrackPk_ >= 0) {
    activeSong = &songLibrary.getSongByPK(currentTrackPk_);
  }
  DisplayExpressionEvalContext context(statusRuntimeSymbols_, activeSong);
  return runtimeValueToQString(windowTitleExpr_->evaluateValue(context));
}

void MainWindow::updateOpenSettingsDisplayPreviewContext() {
  SettingsDialog *dialog = findChild<SettingsDialog *>();
  if (!dialog) {
    return;
  }
  const MSong *activeSong = nullptr;
  if (currentTrackPk_ >= 0) {
    activeSong = &songLibrary.getSongByPK(currentTrackPk_);
  }
  dialog->setDisplayExpressionPreviewContext(activeSong, statusRuntimeSymbols_);
}

void MainWindow::updatePlaybackTimeStatus() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    statusBar()->clearMessage();
    setWindowTitle(defaultWindowTitle_);
    updateStatusRuntimeSymbols();
    updateOpenSettingsDisplayPreviewContext();
    return;
  }
  updateStatusRuntimeSymbols();
  updateOpenSettingsDisplayPreviewContext();
  statusBar()->showMessage(evaluateStatusBarExpression());
  setWindowTitle(evaluateWindowTitleExpression());
}

void MainWindow::seek(int mseconds) {
  if (!playlistReady_) {
    return;
  }
  lastPositionSampleMs_ = -1;
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
    // displayErrorMessage();
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
      R"(Audio Files (*.mp3 *.flac *.m4a *.wav *.ogg *.opus *.alac);;All Files (*.*))");
  for (const QString &s : fileName) {
    playlistTabs->currentPlaylist()->addSong(
        songLibrary.loadSongFromFile(s.toStdString()));
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
  QStringList filters = {"*.mp3", "*.flac", "*.m4a", "*.wav",
                         "*.ogg", "*.opus", "*.alac"};
  const QStringList files = dir.entryList(filters, QDir::Files);

  QProgressDialog progressDialog("Loading songs from folder...", QString{}, 0,
                                 static_cast<int>(files.size()), this);
  progressDialog.setWindowModality(Qt::WindowModal);
  progressDialog.setCancelButton(nullptr);
  progressDialog.setMinimumDuration(0);
  progressDialog.setValue(0);

  std::vector<MSong> songs;
  songs.reserve(static_cast<size_t>(files.size()));
  for (int i = 0; i < files.size(); ++i) {
    const QString &s = files[i];
    songs.push_back(
        songLibrary.loadSongFromFile(dir.absoluteFilePath(s).toStdString()));
    progressDialog.setValue(i + 1);
    QApplication::processEvents();
  }
  progressDialog.setValue(static_cast<int>(files.size()));
  playlistTabs->currentPlaylist()->addSongs(std::move(songs));
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

void MainWindow::resetPlayStatsSession(int songPk) {
  currentTrackPk_ = songPk;
  sessionDurationMs_ = currentDurationMs_;
  sessionMaxPositionMs_ = 0;
  sessionListenedMs_ = 0;
  lastPositionSampleMs_ = -1;
  completionCounted_ = false;
}

void MainWindow::maybeCountCompletedPlay() {
  if (completionCounted_ || currentTrackPk_ < 0) {
    return;
  }
  const qint64 duration = sessionDurationMs_;
  if (duration <= 0) {
    return;
  }
  const qint64 nearEndThreshold =
      std::max<qint64>(0, std::min((duration * 9) / 10, duration - 5000));
  const qint64 listenedThreshold = (duration * 2) / 3;
  if (sessionMaxPositionMs_ < nearEndThreshold ||
      sessionListenedMs_ < listenedThreshold) {
    return;
  }
  if (!songLibrary.incrementPlayCount(currentTrackPk_)) {
    return;
  }
  completionCounted_ = true;
  playlistTabs->notifySongDataChangedInAllPlaylists(currentTrackPk_);
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

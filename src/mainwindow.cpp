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
#include "windowsmediacenter.h"
#endif
#include "databasemanager.h"
#include "librarysearchdialog.h"
#include "settingsdialog.h"
#include "songparser.h"
#include <QActionGroup>
#include <QApplication>
#include <QDebug>
#include <QFileDialog>
#include <QProgressDialog>
#include <QSettings>
#include <QSqlDatabase>
#include <QStandardPaths>
#include <QStatusBar>

namespace {
QString formatPlaybackTime(qint64 milliseconds) {
  const qint64 totalSeconds = std::max<qint64>(0, milliseconds / 1000);
  const qint64 hours = totalSeconds / 3600;
  const qint64 minutes = (totalSeconds % 3600) / 60;
  const qint64 seconds = totalSeconds % 60;

  if (hours > 0) {
    return QStringLiteral("%1:%2:%3")
        .arg(hours)
        .arg(minutes, 2, 10, QLatin1Char('0'))
        .arg(seconds, 2, 10, QLatin1Char('0'));
  }

  return QStringLiteral("%1:%2")
      .arg(minutes, 2, 10, QLatin1Char('0'))
      .arg(seconds, 2, 10, QLatin1Char('0'));
}
} // namespace

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), control{playbackQueue_},
      columnLayoutManager_(columnRegistry_, this),
      databaseManager_(columnRegistry_),
      songLibrary(columnRegistry_, databaseManager_), lyricsManager{this} {
  ui->setupUi(this);
  updatePlaybackTimeStatus();
  setUpMenuBar();
  setUpPlaylist();
  initSettings();
  initPlaybackBackend();
  setUpPlaybackActions();
  setUpLyricsPanel();
  setUpSplitter();
  setupSystemMediaInterface();
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
  for (QAction *act : {ui->actionDefault, ui->actionShuffle_tracks}) {
    act->setCheckable(true);
    playbackOrderMenuActionGroup->addAction(act);
  }
  ui->actionDefault->setChecked(true);
  connect(ui->actionSearch, &QAction::triggered, this,
          &MainWindow::openLibrarySearchDialog);
}

void MainWindow::setUpPlaylist() {
  playlistTabs = ui->playlistTabs;
  QSqlDatabase &db = databaseManager_.db();
  if (!columnRegistry_.loadDynamicColumns(db)) {
    qFatal("setUpPlaylist: failed to load dynamic columns");
  }
  columnLayoutManager_.refreshFromRegistry();
  songLibrary.loadFromDatabase();
  playlistTabs->init(&songLibrary, &playbackQueue_, &control,
                     playbackOrderMenuActionGroup, &columnRegistry_,
                     &columnLayoutManager_);
  connect(playlistTabs, &PlaylistTabs::doubleClicked, [this](QModelIndex i) {
    MSong song = control.playIndex(i.row());
    playSong(song);
    setUpImageAndLyrics(song);
  });
  // playlist operations
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionAdd_folder, &QAction::triggered, this,
          &MainWindow::openFolder);
  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
  connect(ui->actionClear_Playlist, &QAction::triggered,
          playlistTabs->currentPlaylist(), &Playlist::clear);
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
  connect(backendManager->player(), &AudioPlayer::positionChanged,
          &lyricsManager, &LyricsManager::onPlayerProgressChange);
  connect(&lyricsManager, &LyricsManager::newLyricsLineIndex, ui->lyricsPanel,
          &LyricsPanel::updateLyricsPanel);
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
}

void MainWindow::playSong(const MSong &song) {
  const std::string filepath = song.at("filepath").text;
  if (filepath.empty()) {
    qFatal("playSong: filepath is empty");
  }
  const MSong &activeSong = songLibrary.refreshSongFromFile(filepath);
  playlistTabs->notifySongDataChangedInAllPlaylists(filepath);

  backendManager->player()->setSource(
      QString::fromStdString(activeSong.at("filepath").text));
  backendManager->player()->play();
  sysMedia->setTitleAndArtist(
      QString::fromStdString(activeSong.at("title").text),
      QString::fromStdString(activeSong.at("artist").text));
  control.play();
  updatePlaybackTimeStatus();
}

void MainWindow::next() {
  const auto &[song, row, pl] = control.next();
  if (row < 0)
    return;

  playSong(song);
  navigateIndex(song, row, pl);
}

void MainWindow::prev() {
  const auto &[song, row, pl] = control.prev();
  if (row < 0)
    return;

  playSong(song);
  navigateIndex(song, row, pl);
}

void MainWindow::play() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    int lastPlayedPk = playlistTabs->currentPlaylist()->getLastPlayed();
    MSong song = control.playIndex(
        playlistTabs->currentPlaylist()->getIndexByPk(lastPlayedPk));
    playSong(song);
  } else {
    backendManager->player()->play();
    control.play();
    updatePlaybackTimeStatus();
    sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Playing);
  }
}

void MainWindow::pause() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None)
    return;
  backendManager->player()->pause();
  control.pause();
  updatePlaybackTimeStatus();
  sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Paused);
}

void MainWindow::toggle() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::Playing)
    pause();
  else
    play();
}

void MainWindow::stop() {
  backendManager->player()->stop();
  control.stop();
  updatePlaybackTimeStatus();
  sysMedia->setPlaybackState(ISystemMediaInterface::PlaybackState::Stopped);
}

void MainWindow::navigateIndex(MSong song, int row, Playlist *pl) {
  playlistTabs->navigateIndex(song, row, pl);
  setUpImageAndLyrics(song);
}

void MainWindow::setUpImageAndLyrics(MSong song) {
  lyricsManager.setLyrics(
      lyricsLoader.getLyrics(song.at("title").text, song.at("artist").text));
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
  AddEntryDialog *addEntryDialog = new AddEntryDialog(this);
  addEntryDialog->show();
  addEntryDialog->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(addEntryDialog, &AddEntryDialog::entryStringEntered,
                   [this](const QString &text) {
                     playlistTabs->currentPlaylist()->addSong(SongParser::parse(
                         text.toStdString(), columnRegistry_));
                   });
}

void MainWindow::durationChanged(qint64 newDuration) {
  currentDurationMs_ = newDuration;
  ui->horizontalSlider->setMaximum(newDuration);
  updatePlaybackTimeStatus();
  sysMedia->setDuration(newDuration);
}

void MainWindow::positionChanged(qint64 progress) {
  currentPositionMs_ = progress;
  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
  updatePlaybackTimeStatus();
  sysMedia->updateCurrentPosition(progress);
}

void MainWindow::updatePlaybackTimeStatus() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    statusBar()->clearMessage();
    return;
  }
  statusBar()->showMessage(
      QStringLiteral("%1 / %2").arg(formatPlaybackTime(currentPositionMs_),
                                    formatPlaybackTime(currentDurationMs_)));
}

void MainWindow::seek(int mseconds) {
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
    QApplication::alert(this);
    this->next();
    break;
  case QMediaPlayer::InvalidMedia:
    // displayErrorMessage();
    break;
  }
}

void MainWindow::open() {
  const QList<QString> fileName = QFileDialog::getOpenFileNames(
      this, "Open the file",
      QStandardPaths::writableLocation(QStandardPaths::MusicLocation),
      R"(Audio Files (*.mp3 *.flac *.m4a *.wav *.ogg *.opus *.alac);;All Files (*.*))");
  for (const QString &s : fileName) {
    playlistTabs->currentPlaylist()->addSong(
        SongParser::parse(s.toStdString(), columnRegistry_));
  }
}

void MainWindow::openFolder() {
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
    songs.push_back(SongParser::parse(dir.absoluteFilePath(s).toStdString(),
                                      columnRegistry_));
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

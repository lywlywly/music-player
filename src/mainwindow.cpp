#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#ifdef Q_OS_MACOS
#include "macosmediacenter.h"
#elifdef Q_OS_LINUX
#include "mprismediainterface.h"
#else
#include "dummysystemmediainterface.h"
#endif
#include "settingsdialog.h"
#include "songparser.h"
#include <QActionGroup>
#include <QDebug>
#include <QFileDialog>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), control{playbackQueue_},
      lyricsManager{this} {
  ui->setupUi(this);
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
#ifdef Q_OS_MACOS
  sysMedia = new MacOSMediaCenter(this);
#elifdef Q_OS_LINUX
  sysMedia = new MprisMediaInterface(this);
#else
  sysMedia = new DummySystemMediaInterface(this);
#endif

  connect(sysMedia, &ISystemMediaInterface::playRequested, this,
          &MainWindow::play);
  connect(sysMedia, &ISystemMediaInterface::pauseRequested, this,
          &MainWindow::pause);
  connect(sysMedia, &ISystemMediaInterface::nextRequested, this,
          &MainWindow::next);
  connect(sysMedia, &ISystemMediaInterface::previousRequested, this,
          &MainWindow::prev);
}

void MainWindow::setUpMenuBar() {
  playbackOrderMenuActionGroup = new QActionGroup(this);
  playbackOrderMenuActionGroup->setExclusive(true);
  for (QAction *act : {ui->actionDefault, ui->actionShuffle_tracks}) {
    act->setCheckable(true);
    playbackOrderMenuActionGroup->addAction(act);
  }
  ui->actionDefault->setChecked(true);
}

void MainWindow::setUpPlaylist() {
  playlistTabs = ui->playlistTabs;
  playlistTabs->init(&songLibrary, &playbackQueue_, &control,
                     playbackOrderMenuActionGroup);
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
    SettingsDialog dialog(this);
    connect(&dialog, &SettingsDialog::backendChanged, this,
            [this](PlaybackBackendManager::Backend backend) {
              if (backendManager->currentBackend() != backend) {
                backendManager->setBackend(backend);
                control.stop();
                setUpPlaybackBackend();
              }
            });
    dialog.exec();
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
  connect(backendManager->player(), &AudioPlayer::mediaStatusChanged, this,
          &MainWindow::statusChanged);
  connect(backendManager->player(), &AudioPlayer::durationChanged, this,
          &MainWindow::durationChanged);
  connect(backendManager->player(), &AudioPlayer::positionChanged, this,
          &MainWindow::positionChanged);
}

void MainWindow::playSong(const MSong &song) {
  backendManager->player()->setSource(QString::fromStdString(song.at("path")));
  backendManager->player()->play();
  sysMedia->setTrackInfo(QString::fromStdString(song.at("title")),
                         QString::fromStdString(song.at("artist")), 0);
  control.play();
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
    sysMedia->updatePlaybackState(true);
  }
}

void MainWindow::pause() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None)
    return;
  backendManager->player()->pause();
  control.pause();
  sysMedia->updatePlaybackState(false);
}

void MainWindow::stop() {
  backendManager->player()->stop();
  control.stop();
  sysMedia->updatePlaybackState(false);
}

void MainWindow::navigateIndex(MSong song, int row, Playlist *pl) {
  playlistTabs->navigateIndex(song, row, pl);
  setUpImageAndLyrics(song);
}

void MainWindow::setUpImageAndLyrics(MSong song) {
  lyricsManager.setLyrics(
      lyricsLoader.getLyrics(song.at("title"), song.at("artist")));
  auto [data, size] = SongParser::extractCoverImage(song.at("path"));
  if (size > 0) {
    pixmap.loadFromData(data.data(), size);
    ui->label->setPixmap(pixmap.scaled(ui->splitter->sizes().first() - 10,
                                       ui->splitter_2->sizes().last() - 10,
                                       Qt::KeepAspectRatio));
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
                     playlistTabs->currentPlaylist()->addSong(
                         SongParser::parse(text.toStdString()));
                   });
}

void MainWindow::durationChanged(qint64 newDuration) {
  duration = newDuration;
  ui->horizontalSlider->setMaximum(duration);
}

void MainWindow::positionChanged(qint64 progress) {
  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
}

void MainWindow::seek(int mseconds) {
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
  QList<QString> fileName = QFileDialog::getOpenFileNames(
      this, "Open the file", QDir::homePath() + "/Music");
  for (QString s : fileName) {
    playlistTabs->currentPlaylist()->addSong(
        SongParser::parse(s.toStdString()));
  }
}

void MainWindow::openFolder() {
  QString dirPath = QFileDialog::getExistingDirectory(this, "Select Folder");

  if (dirPath.isEmpty())
    return;

  QDir dir(dirPath);
  QStringList files = dir.entryList(QDir::Files);

  std::vector<MSong> songs;
  for (QString s : files) {
    if (s.split(".").last() == "mp3") {
      // more file types
      songs.push_back(SongParser::parse(dir.absoluteFilePath(s).toStdString()));
    }
  }
  playlistTabs->currentPlaylist()->addSongs(std::move(songs));
}

MainWindow::~MainWindow() { delete ui; }

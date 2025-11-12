#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#include "settingsdialog.h"
#include "songparser.h"
#include <QActionGroup>
#include <QDebug>
#include <QFileDialog>
#include <QKeyEvent>
#include <QSettings>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), control{playbackQueue_},
      lyricsManager{this} {
  ui->setupUi(this);
  setUpMenuBar();
  setUpPlaylist();
  initSettings();
  setUpPlaybackManager();
  initPlaybackBackend();
  setUpPlaybackActions();
  setUpTableView(currentPlaylist, currentTableView);
  setUpLyricsPanel();
  setUpSplitter();
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
  // context menu
  playNextAction = playlistContextMenu.addAction("Play Next");
  playEndAction = playlistContextMenu.addAction("Add to Playback Queue");
  // tabs
  ui->tabWidget->tabBar()->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tabWidget->tabBar(), &QTabBar::customContextMenuRequested, this,
          &MainWindow::onTabContextMenuRequested);
  connect(ui->tabWidget, &QTabWidget::currentChanged, this,
          &MainWindow::onTabChanged);
  ui->tabWidget->setTabText(0, "Default");
  // default playlist
  playlistMap.emplace(
      std::piecewise_construct, std::forward_as_tuple("Default"),
      std::forward_as_tuple(SongStore{songLibrary}, playbackQueue_));
  QWidget *tabWidget = ui->tabWidget->widget(0);
  currentTableView = tabWidget->findChild<QTableView *>();
  currentPlaylist = &playlistMap.at("Default");
  currentPlaylist->registerStatusUpdateCallback();
  setUpCurrentPlaylist();
  // playlist operations
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionAdd_folder, &QAction::triggered, this,
          &MainWindow::openFolder);
  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
  connect(ui->actionClear_Playlist, &QAction::triggered, currentPlaylist,
          &Playlist::clear);
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

void MainWindow::setUpPlaybackManager() {
  connect(playbackOrderMenuActionGroup, &QActionGroup::triggered, this,
          [this](QAction *action) {
            QAction *checked = playbackOrderMenuActionGroup->checkedAction();
            QString selectedText = checked->text();
            control.setPolicy(string2Policy(selectedText));
          });
  connect(playNextAction, &QAction::triggered, this, [&]() {
    QModelIndex index = playNextAction->data().value<QModelIndex>();
    control.queueStart(index.row());
  });
  connect(playEndAction, &QAction::triggered, this, [&]() {
    QModelIndex index = playNextAction->data().value<QModelIndex>();
    control.queueEnd(index.row());
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

void MainWindow::setUpTableView(Playlist *pl, QTableView *tbv) {
  tbv->setModel(pl);
  tbv->setSortingEnabled(true);
  tbv->setEditTriggers(QAbstractItemView::NoEditTriggers);
  tbv->setSelectionBehavior(QAbstractItemView::SelectRows);
  tbv->installEventFilter(this);
  tbv->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(tbv, &QTableView::customContextMenuRequested, this,
          &MainWindow::onCustomContextMenuRequested);
  tbv->horizontalHeader()->setSortIndicatorShown(false);
  QObject::connect(tbv->horizontalHeader(), &QHeaderView::sectionClicked, this,
                   [=, this](int idx) {
                     tbv->horizontalHeader()->setSortIndicatorShown(true);
                     pl->sortByField(
                         fieldStringList[idx].toStdString(),
                         tbv->horizontalHeader()->sortIndicatorOrder());
                   });
  connect(tbv, &QTableView::doubleClicked, [this](QModelIndex i) {
    MSong song = control.playIndex(i.row());
    playSong(QString::fromUtf8(song.at("path")));
    setUpImageAndLyrics(song);
  });
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

void MainWindow::onTabContextMenuRequested(const QPoint &pos) {
  QTabBar *tabBar = ui->tabWidget->tabBar();
  int index = tabBar->tabAt(pos);

  QMenu menu(this);
  QAction *addAction = menu.addAction("Add New Playlist");
  QAction *removeAction = nullptr;

  if (index != -1)
    removeAction = menu.addAction("Remove Tab");

  QAction *selected = menu.exec(tabBar->mapToGlobal(pos));

  if (selected == addAction) {
    QWidget *newTab = new QWidget;
    QGridLayout *layout = new QGridLayout(newTab);
    QTableView *tbv = new QTableView(this);
    layout->addWidget(tbv);
    QString newPlaylistName = getNewPlaylistName();
    ui->tabWidget->addTab(newTab, newPlaylistName);
    playlistMap.emplace(
        std::piecewise_construct,
        std::forward_as_tuple(std::string(newPlaylistName.toStdString())),
        std::forward_as_tuple(SongStore{songLibrary}, playbackQueue_));
    Playlist &pl = playlistMap.at(newPlaylistName.toStdString());

    setUpTableView(&pl, tbv);
    pl.registerStatusUpdateCallback();
  } else if (selected == removeAction) {
    std::string txt = ui->tabWidget->tabText(index).toStdString();
    ui->tabWidget->removeTab(index);
    playlistMap.erase(txt);
  }
}

void MainWindow::onTabChanged(int index) {
  QString text = ui->tabWidget->tabText(index);
  qDebug() << "Switched to tab" << index << ":" << text;
  currentPlaylist = &playlistMap.at(text.toStdString());
  QWidget *tab = ui->tabWidget->widget(index);

  if (QTableView *tbv = tab->findChild<QTableView *>()) {
    currentTableView = tbv;
  }

  setUpCurrentPlaylist();
}

void MainWindow::next() {
  const auto &[song, row, pl] = control.next();
  if (row < 0)
    return;

  playSong(QString::fromUtf8(song.at("path")));
  control.play();
  navigateIndex(song, row,
                findPlaylistIndex(QString::fromUtf8(findPlaylistName(pl))));
}

void MainWindow::prev() {
  const auto &[song, row, pl] = control.prev();
  if (row < 0)
    return;

  playSong(QString::fromUtf8(song.at("path")));
  control.play();
  navigateIndex(song, row,
                findPlaylistIndex(QString::fromUtf8(findPlaylistName(pl))));
}

void MainWindow::play() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None) {
    MSong song = control.playIndex(currentPlaylist->getLastPlayed());
    playSong(QString::fromUtf8(song.at("path")));
  } else {
    backendManager->player()->play();
    control.play();
  }
}

void MainWindow::pause() {
  if (control.getStatus() == PlaybackQueue::PlaybackStatus::None)
    return;
  backendManager->player()->pause();
  control.pause();
}

void MainWindow::stop() {
  backendManager->player()->stop();
  control.stop();
}

void MainWindow::playSong(const QUrl &url) {
  backendManager->player()->setSource(url);
  backendManager->player()->play();
}

void MainWindow::navigateIndex(MSong song, int row, int tabIdx) {
  if (tabIdx >= 0)
    ui->tabWidget->setCurrentIndex(tabIdx);
  QModelIndex index = currentTableView->model()->index(row, 0);
  currentTableView->selectionModel()->select(
      index, QItemSelectionModel::Select | QItemSelectionModel::Rows);
  currentTableView->setCurrentIndex(index);
  currentTableView->scrollTo(index);
  setUpImageAndLyrics(song);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (obj == currentTableView && event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Backspace) {
      QModelIndex index = currentTableView->currentIndex();
      if (index.isValid()) {
        currentPlaylist->removeSong(index.row());
      }
      return true;
    }
  }
  return QObject::eventFilter(obj, event);
}

QString MainWindow::getNewPlaylistName() {
  if (playlistMap.find("New Playlist") == playlistMap.end())
    return "New Playlist";
  for (int i = 1;; i++) {
    std::string newName = std::format("New Playlist {}", i);
    if (playlistMap.find(newName) == playlistMap.end())
      return QString::fromUtf8(newName);
  }
}

Policy MainWindow::string2Policy(QString s) {
  if (s == "Default")
    return Policy::Sequential;
  else if (s == "Shuffle (tracks)")
    return Policy::Shuffle;
  throw std::logic_error("Never");
}

std::string MainWindow::findPlaylistName(Playlist *pl) {
  for (const auto &[key, value] : playlistMap) {
    if (&value == pl)
      return key;
  }
  throw std::logic_error("Never");
}

int MainWindow::findPlaylistIndex(QString s) {
  for (int i = 0; i < ui->tabWidget->count(); ++i) {
    QString text = ui->tabWidget->tabText(i);
    if (text == s)
      return i;
  }
  throw std::logic_error("Never");
}

void MainWindow::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = currentTableView->indexAt(pos);
  if (!index.isValid())
    return;

  playNextAction->setData(QVariant::fromValue(index));
  playEndAction->setData(QVariant::fromValue(index));

  playlistContextMenu.exec(currentTableView->viewport()->mapToGlobal(pos));
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

void MainWindow::setUpCurrentPlaylist() {
  control.setView(currentPlaylist);
  QAction *checked = playbackOrderMenuActionGroup->checkedAction();
  QString selectedText = checked->text();
  control.setPolicy(string2Policy(selectedText));
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
                     currentPlaylist->addSong(
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
    currentPlaylist->addSong(SongParser::parse(s.toStdString()));
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
  currentPlaylist->addSongs(std::move(songs));
}

MainWindow::~MainWindow() { delete ui; }

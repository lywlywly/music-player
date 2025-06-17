#include "mainwindow.h"

#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#include "qevent.h"
#include <QAudioOutput>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProcess>
#include <QScrollBar>
#include <QSortFilterProxyModel>
#include <QStandardItem>
#include <QTextBlock>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), control{playbackQueue},
      playlist{SongStore{songLibrary}, playbackQueue} {
  ui->setupUi(this);
  playlist.registerStatusUpdateCallback();
  control.setView(&playlist);
  createModels();
  setUpPlaylist();
  setUpPlaybackControl();
  setUpTableView();
  setUpPlayer();
  setUpSlider();
  setUpLyricsPanel();
  setUpPlaybackActions();
  setUpSplitter();

  orderGroup = new QActionGroup(this);
  orderGroup->setExclusive(true);
  for (QAction *act : {ui->actionDefault, ui->actionShuffle_tracks}) {
    act->setCheckable(true);
    orderGroup->addAction(act);
  }

  ui->actionDefault->setChecked(true); // Set default
  QAction *checked = orderGroup->checkedAction();
  QString selectedText = checked->text();
  control.setPolicy(string2Policy(selectedText));

  connect(orderGroup, &QActionGroup::triggered, this, [this](QAction *action) {
    QAction *checked = orderGroup->checkedAction();
    QString selectedText = checked->text();
    control.setPolicy(string2Policy(selectedText));
  });

  playNextAction = contextMenu.addAction("Play Next");
  playEndAction = contextMenu.addAction("Add to Playback Queue");
  connect(playNextAction, &QAction::triggered, this, [&]() {
    QModelIndex index = playNextAction->data().value<QModelIndex>();
    control.queueStart(index.row());
  });
  connect(playEndAction, &QAction::triggered, this, [&]() {
    QModelIndex index = playNextAction->data().value<QModelIndex>();
    control.queueEnd(index.row());
  });
}

void MainWindow::setUpSplitter() {
  connect(ui->splitter, &QSplitter::splitterMoved, this,
          &MainWindow::updateImageSize);
  connect(ui->splitter_2, &QSplitter::splitterMoved, this,
          &MainWindow::updateImageSize);
}

void MainWindow::setUpPlaybackActions() {
  connect(ui->actionPlay, &QAction::triggered, this, &MainWindow::playSong);
  connect(ui->play_button, &QAbstractButton::clicked, this,
          &MainWindow::playSong);
  connect(ui->actionPause, &QAction::triggered, this, &MainWindow::pauseSong);
  connect(ui->pause_button, &QAbstractButton::clicked, this,
          &MainWindow::pauseSong);
  connect(ui->next_button, &QAbstractButton::clicked, this, &MainWindow::next);
  connect(ui->prev_button, &QAbstractButton::clicked, this, &MainWindow::prev);
  // connect(ui->random_button, &QAbstractButton::clicked, &pb,
  //         &PlaybackState::shuffle);
}

void MainWindow::next() {
  auto [song, row] = control.next();
  if (row < 0) {
    // empty playlist, do nothing
    qDebug() << "next" << "empty playlist, do nothing";
    return;
  }
  QUrl url = QString::fromUtf8(song.at("path"));
  mediaPlayer.setSource(url);
  mediaPlayer.play();
  QModelIndex index =
      ui->tableView->model()->index(row, 0); // column 0 is enough
  ui->tableView->selectionModel()->select(index, QItemSelectionModel::Select |
                                                     QItemSelectionModel::Rows);
  // Optional: set focus and scroll to it
  ui->tableView->setCurrentIndex(index);
  ui->tableView->scrollTo(index);
  setUpImage(song);
}

void MainWindow::prev() {
  auto [song, row] = control.prev();
  if (row < 0) {
    // empty playlist, do nothing
    qDebug() << "prev" << "empty playlist, do nothing";
    return;
  }
  QUrl url = QString::fromUtf8(song.at("path"));
  mediaPlayer.setSource(url);
  mediaPlayer.play();
  QModelIndex index =
      ui->tableView->model()->index(row, 0); // column 0 is enough
  ui->tableView->selectionModel()->select(index, QItemSelectionModel::Select |
                                                     QItemSelectionModel::Rows);
  // Optional: set focus and scroll to it
  ui->tableView->setCurrentIndex(index);
  ui->tableView->scrollTo(index);
  setUpImage(song);
}

void MainWindow::setUpSlider() {
  ui->horizontalSlider->setRange(0, mediaPlayer.duration());
  connect(ui->horizontalSlider, &QSlider::sliderMoved, this, &MainWindow::seek);
  connect(&mediaPlayer, &QMediaPlayer::durationChanged, this,
          &MainWindow::durationChanged);
  connect(&mediaPlayer, &QMediaPlayer::positionChanged, this,
          &MainWindow::positionChanged);
}

void MainWindow::setUpLyricsPanel() {
  lyricsLoader = new LyricsLoader(this);
  lyricsManager = new LyricsManager(this);
  connect(&mediaPlayer, &QMediaPlayer::positionChanged, lyricsManager,
          &LyricsManager::onPlayerProgressChange);
  connect(lyricsManager, &LyricsManager::newLyricsLineIndex, this,
          &MainWindow::updateLyricsPanel);
}

bool MainWindow::eventFilter(QObject *obj, QEvent *event) {
  if (obj == ui->tableView && event->type() == QEvent::KeyPress) {
    QKeyEvent *keyEvent = static_cast<QKeyEvent *>(event);
    if (keyEvent->key() == Qt::Key_Backspace) {
      QModelIndex index = ui->tableView->currentIndex();
      if (index.isValid()) {
        playlist.removeSong(index.row());
      }
      return true;
    }
  }
  return QObject::eventFilter(obj, event);
}

Policy MainWindow::string2Policy(QString s) {
  if (s == "Default")
    return Policy::Sequential;
  else if (s == "Shuffle (tracks)")
    return Policy::Shuffle;
  throw std::logic_error("Never");
}

void MainWindow::setUpTableView() {
  ui->textEdit->setReadOnly(true);
  ui->tableView->setModel(&playlist);
  ui->tableView->setSortingEnabled(true);
  ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  ui->tableView->installEventFilter(this);
  ui->tableView->setContextMenuPolicy(Qt::CustomContextMenu);
  connect(ui->tableView, &QTableView::customContextMenuRequested, this,
          &MainWindow::onCustomContextMenuRequested);
}

void MainWindow::onCustomContextMenuRequested(const QPoint &pos) {
  QModelIndex index = ui->tableView->indexAt(pos);
  if (!index.isValid())
    return;

  playNextAction->setData(QVariant::fromValue(index));
  playEndAction->setData(QVariant::fromValue(index));

  contextMenu.exec(ui->tableView->viewport()->mapToGlobal(pos));
}

void MainWindow::createModels() {}

void MainWindow::setUpPlaybackControl() {
  connect(ui->tableView, &QTableView::doubleClicked, [this](QModelIndex i) {
    MSong song = control.play(i.row());
    mediaPlayer.setSource(QString::fromUtf8(song.at("path")));
    mediaPlayer.play();
    setUpImage(song);
  });
}

void MainWindow::setUpImage(MSong song) {
  lyricsManager->setCurrentLyricsMap(
      lyricsLoader->getLyrics(QString::fromUtf8(song.at("title")),
                              QString::fromUtf8(song.at("artist"))));
  auto [data, size] =
      parser->parseSongCover(QString::fromUtf8(song.at("path")));
  if (size > 0) {
    pixmap.loadFromData(data.get(), size);
    ui->label->setPixmap(pixmap.scaled(ui->splitter->sizes().first() - 10,
                                       ui->splitter_2->sizes().last() - 10,
                                       Qt::KeepAspectRatio));
  } else {
    ui->label->setText("No cover");
  }
  resetLyricsPanel();
}

void MainWindow::setUpPlaylist() {
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
  connect(ui->actionClear_Playlist, &QAction::triggered, &playlist,
          &Playlist::clear);
}

void MainWindow::setUpPlayer() {
  auto audioOutput = new QAudioOutput;
  mediaPlayer.setAudioOutput(audioOutput);
  // audioOutput->setVolume(50);
  connect(&mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
          &MainWindow::statusChanged);
}

void MainWindow::resetLyricsPanel() {
  ui->textEdit->clear();
  const QMap<int, QString> &map = lyricsManager->getCurrentLyricsMap();
  for (auto value : map.values()) {
    ui->textEdit->append(value);
  }
}

void getVisibleTextRange(QTextEdit *textEdit) {
  QTextCursor cursor = textEdit->textCursor();
  int cursorPosition = cursor.position();

  QScrollBar *verticalScrollBar = textEdit->verticalScrollBar();
  qDebug() << verticalScrollBar->value();

  qDebug() << cursor.blockNumber() << cursorPosition;
}

void MainWindow::updateLyricsPanel(int index) {
  // Get the text cursor of the QTextEdit
  QTextCursor cursor = ui->textEdit->textCursor();
  QTextBlockFormat format = cursor.blockFormat();

  cursor.movePosition(QTextCursor::Start);
  cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, index);
  format = cursor.blockFormat();
  format.setBackground(QColor(Qt::blue));
  cursor.setBlockFormat(format);

  cursor.movePosition(QTextCursor::Start);
  cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, index - 1);
  format = cursor.blockFormat();
  format.setBackground(QColor(Qt::transparent));
  cursor.setBlockFormat(format);
  cursor.movePosition(QTextCursor::Down, QTextCursor::MoveAnchor, 1);

  ui->textEdit->setTextCursor(cursor);
  ui->textEdit->ensureCursorVisible();

  // getVisibleTextRange(ui->textEdit);
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
  QObject::connect(
      addEntryDialog, &AddEntryDialog::entryStringEntered,
      [this](const QString &text) { playlist.addSong(parser->parse(text)); });
}

void MainWindow::playSong() { mediaPlayer.play(); }

void MainWindow::pauseSong() { mediaPlayer.pause(); }

void MainWindow::durationChanged(qint64 newDuration) {
  duration = newDuration;
  ui->horizontalSlider->setMaximum(duration);
}

void MainWindow::positionChanged(qint64 progress) {
  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
}

void MainWindow::seek(int mseconds) { mediaPlayer.setPosition(mseconds); }

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
  qDebug() << "opening file" << fileName;
  for (QString s : fileName) {
    playlist.addSong(parser->parse(s));
  }
}

MainWindow::~MainWindow() { delete ui; }

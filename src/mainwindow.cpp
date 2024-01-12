#include "mainwindow.h"

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

#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#include "myproxymodel.h"
#include "mytableheader.h"
#include "qevent.h"
#include "searchdialog.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  createModels();
  setUpPlaylist();
  setUpPlaybackControl();
  setUpTableView();
  setUpPlayer();
  setUpSlider();
  setUpLyricsPanel();
  setUpPlaybackActions();
  setUpSplitter();
}

void MainWindow::setUpSplitter() {
  connect(ui->splitter, &QSplitter::splitterMoved, this,
          &MainWindow::updateImageSize);
  connect(ui->splitter_2, &QSplitter::splitterMoved, this,
          &MainWindow::updateImageSize);
}

void MainWindow::setUpPlaybackActions() {
  connect(ui->actionPlay, &QAction::triggered, this, &MainWindow::playSong);
  connect(ui->actionPlay, &QAction::triggered, this, &MainWindow::playSong);
  connect(ui->play_button, &QAbstractButton::clicked, this,
          &MainWindow::playSong);
  connect(ui->actionPause, &QAction::triggered, this, &MainWindow::pauseSong);
  connect(ui->pause_button, &QAbstractButton::clicked, this,
          &MainWindow::pauseSong);
  connect(ui->next_button, &QAbstractButton::clicked, control,
          &PlayerControlModel::next);
  connect(ui->prev_button, &QAbstractButton::clicked, control,
          &PlayerControlModel::previous);
  connect(ui->random_button, &QAbstractButton::clicked, control,
          &PlayerControlModel::shuffle);
}

void MainWindow::setUpSlider() {
  ui->horizontalSlider->setRange(0, mediaPlayer->duration());
  connect(ui->horizontalSlider, &QSlider::sliderMoved, this, &MainWindow::seek);
  connect(mediaPlayer, &QMediaPlayer::durationChanged, this,
          &MainWindow::durationChanged);
  connect(mediaPlayer, &QMediaPlayer::positionChanged, this,
          &MainWindow::positionChanged);
}

void MainWindow::setUpLyricsPanel() {
  lyricsLoader = new LyricsLoader(this);
  lyricsManager = new LyricsManager(this);
  connect(mediaPlayer, &QMediaPlayer::positionChanged, lyricsManager,
          &LyricsManager::onPlayerProgressChange);
  connect(lyricsManager, &LyricsManager::newLyricsLineIndex, this,
          &MainWindow::updateLyricsPanel);
}

void MainWindow::setUpTableView() {
  ui->textEdit->setReadOnly(true);
  ui->tableView->setModel(proxyModel);
  ui->tableView->setSortingEnabled(true);
  ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);
  tableHeader = new MyTableHeader(Qt::Orientation::Horizontal, this);
  ui->tableView->setHorizontalHeader(tableHeader);
  tableHeader->setSectionsClickable(true);
  QObject::connect(
      tableHeader, &QHeaderView::sectionClicked, this,
      [this](int logicalIndex) { proxyModel->toogleSortOrder(logicalIndex); });
}

void MainWindow::createModels() {
  model = new SongTableModel(this);
  proxyModel = new MyProxyModel(this);
  proxyModel->setSourceModel(model);
}

void MainWindow::setUpPlaybackControl() {
  control = new PlayerControlModel(proxyModel);
  connect(model, SIGNAL(playlistChanged()), dynamic_cast<QObject *>(proxyModel),
          SIGNAL(playlistChanged()));
  connect(dynamic_cast<QObject *>(proxyModel), SIGNAL(playlistChanged()),
          control, SLOT(onPlayListChange()));
  connect(control, &PlayerControlModel::indexChange, this, [this]() {
    mediaPlayer->setSource(control->getCurrentUrl());
    mediaPlayer->play();
    Song song = parser->parseFile(control->getCurrentUrl());
    lyricsManager->setCurrentLyricsMap(
        lyricsLoader->getLyrics(song.title, song.artist));
    auto [data, size] = parser->parseSongCover(control->getCurrentUrl());
    if (size > 0) {
      pixmap.loadFromData(data.get(), size);
      ui->label->setPixmap(pixmap.scaled(ui->splitter->sizes().first() - 10,
                                         ui->splitter_2->sizes().last() - 10,
                                         Qt::KeepAspectRatio));
    } else {
      ui->label->setText("No cover");
    }
    resetLyricsPanel();
  });
  connect(control, &PlayerControlModel::indexChange, this,
          [myUI = ui, ctrl = control, md = proxyModel](int idx) {
            md->onPlayListQueueChange(ctrl->getQueue());
            myUI->tableView->selectRow(idx);
          });
  connect(ui->tableView, &QTableView::clicked,
          [this](QModelIndex i) { control->setNext(i.row()); });
  // doubleClicked will also trigger clicked, here set next to -1
  connect(ui->tableView, &QTableView::doubleClicked, [this](QModelIndex i) {
    control->setNext(-1);
    selectEntry(i);
  });
}

void MainWindow::setUpPlaylist() {
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionSave, &QAction::triggered, model, &SongTableModel::save);
  connect(ui->actionLoad_playlist, &QAction::triggered, model,
          &SongTableModel::loadFromUrls);
  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
  connect(ui->actionSearch, &QAction::triggered, this,
          &MainWindow::newSearchDialog);
}

void MainWindow::setUpPlayer() {
  mediaPlayer = new QMediaPlayer(this);
  auto audioOutput = new QAudioOutput;
  mediaPlayer->setAudioOutput(audioOutput);
  // audioOutput->setVolume(50);
  connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
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

void MainWindow::selectEntry(const QModelIndex &index) {
  control->setCurrentIndex(index.row());
  mediaPlayer->setSource(control->getCurrentUrl());
  mediaPlayer->play();
}

void MainWindow::addEntry() {
  AddEntryDialog *addEntryDialog = new AddEntryDialog(this);
  addEntryDialog->show();
  addEntryDialog->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(addEntryDialog, &AddEntryDialog::entryStringEntered,
                   [this](const QString &text) { model->appendSong(text); });
}

void MainWindow::newSearchDialog() {
  SearchDialog *searchDialog = new SearchDialog(this);
  searchDialog->show();
  // also disconnect all connections with searchDialog, every time search dialog
  // is a new one
  searchDialog->setAttribute(Qt::WA_DeleteOnClose);
  connect(searchDialog, &SearchDialog::newSongs, this, &MainWindow::addSongs);
}

void MainWindow::playSong() { mediaPlayer->play(); }

void MainWindow::pauseSong() { mediaPlayer->pause(); }

void MainWindow::durationChanged(qint64 newDuration) {
  duration = newDuration;
  // qDebug() << "new duration" << duration;
  ui->horizontalSlider->setMaximum(duration);
}

void MainWindow::positionChanged(qint64 progress) {
  if (!ui->horizontalSlider->isSliderDown())
    ui->horizontalSlider->setValue(progress);
  // updateDurationInfo(progress / 1000);
}

void MainWindow::seek(int mseconds) {
  // qDebug() << "seek" << mseconds;
  mediaPlayer->setPosition(mseconds);
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
      control->next();
      mediaPlayer->setSource(control->getCurrentUrl());
      mediaPlayer->play();
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
    model->appendSong(s);
  }
}

void MainWindow::addSongs(QList<Song> songs) {
  for (Song s : songs) {
    model->appendSong(s);
  }
}

MainWindow::~MainWindow() { delete ui; }

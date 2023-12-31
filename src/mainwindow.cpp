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
#include <QSortFilterProxyModel>
#include <QStandardItem>
#include <QTextStream>

#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#include "myproxymodel.h"
#include "mytableheader.h"

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow) {
  ui->setupUi(this);

  // initialize models
  model = new SongTableModel(this);
  proxyModel = new MyProxyModel(this);
  proxyModel->setSourceModel(model);
  // initialize player control
  control = new PlayerControlModel(proxyModel);
  connect(model, &SongTableModel::playlistChanged, proxyModel,
          &MyProxyModel::playlistChanged);
  connect(proxyModel, &MyProxyModel::playlistChanged, control,
          &PlayerControlModel::onPlayListChange);
  connect(control, &PlayerControlModel::indexChange, this, [=]() {
    mediaPlayer->setSource(control->getCurrentUrl());
    mediaPlayer->play();
  });
  // intialize tableview
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
  // initialize player
  mediaPlayer = new QMediaPlayer(this);
  auto audioOutput = new QAudioOutput;
  mediaPlayer->setAudioOutput(audioOutput);
  // audioOutput->setVolume(50);
  connect(mediaPlayer, &QMediaPlayer::mediaStatusChanged, this,
          &MainWindow::statusChanged);
  // initialize slider
  ui->horizontalSlider->setRange(0, mediaPlayer->duration());
  connect(ui->horizontalSlider, &QSlider::sliderMoved, this, &MainWindow::seek);
  connect(mediaPlayer, &QMediaPlayer::durationChanged, this,
          &MainWindow::durationChanged);
  connect(mediaPlayer, &QMediaPlayer::positionChanged, this,
          &MainWindow::positionChanged);
  // initialize ui actions
  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
  connect(ui->tableView, &QTableView::clicked,
          [this](QModelIndex i) { control->setNext(i.row()); });
  // doubleClicked will also trigger clicked, here set next to -1
  connect(ui->tableView, &QTableView::doubleClicked, [this](QModelIndex i) {
    control->setNext(-1);
    selectEntry(i);
  });
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
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
  connect(control, &PlayerControlModel::indexChange, this,
          [myUI = ui](int idx) {
            qDebug() << "new index" << idx;
            myUI->tableView->selectRow(idx);
          });
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

void MainWindow::playSong() { mediaPlayer->play(); }

void MainWindow::pauseSong() { mediaPlayer->pause(); }

void MainWindow::durationChanged(qint64 newDuration) {
  duration = newDuration;
  // qDebug() << "new duration" << duration;
  ui->horizontalSlider->setMaximum(duration);
}

void MainWindow::positionChanged(qint64 progress) {
  // qDebug() << "new position" << progress;
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

MainWindow::~MainWindow() { delete ui; }

#include "mainwindow.h"
#include "./ui_mainwindow.h"
#include "addentrydialog.h"
#include <QAudioOutput>
#include <QDebug>
#include <QFile>
#include <QFileDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMediaPlayer>
#include <QMessageBox>
#include <QProcess>
#include <QStandardItem>
#include <QTextStream>

MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::MainWindow), model(this) {
  ui->setupUi(this);
  // model = new QStandardItemModel(4,2,this);
  QList<Field> fields = {Field::ARTIST,  Field::TITLE,       Field::GENRE,
                         Field::BPM,     Field::REPLAY_GAIN, Field::RATING,
                         Field::COMMENT, Field::FILE_PATH};
  model.setColumns(fields);
  tableModel = model.getModel();
  mediaPlayer = new QMediaPlayer(this);

  QMediaPlayer mplayer;
  auto audioOutput = new QAudioOutput;
  mediaPlayer->setAudioOutput(audioOutput);
  // audioOutput->setVolume(50);

  ui->tableView->setModel(tableModel);
  ui->tableView->setEditTriggers(QAbstractItemView::NoEditTriggers);
  ui->tableView->setSelectionBehavior(QAbstractItemView::SelectRows);

  connect(ui->actionAdd, &QAction::triggered, this, &MainWindow::addEntry);
  connect(ui->tableView, &QTableView::doubleClicked, this,
          &MainWindow::selectEntry);
  connect(ui->actionOpen, &QAction::triggered, this, &MainWindow::open);
  connect(ui->actionPlay, &QAction::triggered, this, &MainWindow::playSong);
  connect(ui->actionPause, &QAction::triggered, this, &MainWindow::pauseSong);
}

void MainWindow::selectEntry(const QModelIndex &index) {
  QUrl path = model.getEntryUrl(index);
  qDebug() << model.getEntryUrl(index);
  mediaPlayer->setSource(path);
  mediaPlayer->play();
}

void MainWindow::addEntry() {
  AddEntryDialog *addEntryDialog = new AddEntryDialog(this);
  addEntryDialog->show();
  addEntryDialog->setAttribute(Qt::WA_DeleteOnClose);
  QObject::connect(addEntryDialog, &AddEntryDialog::entryStringEntered,
                   [this](const QString &text) { model.appendSong(text); });
}

void MainWindow::playSong() { mediaPlayer->play(); }

void MainWindow::pauseSong() { mediaPlayer->pause(); }

void MainWindow::open() {
  QString fileName = QFileDialog::getOpenFileName(this, "Open the file");
  qDebug() << "opening file" << fileName;
  model.appendSong(fileName);
}

MainWindow::~MainWindow() { delete ui; }

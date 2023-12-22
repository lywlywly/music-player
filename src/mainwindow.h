#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "qmediaplayer.h"
#include "qstandarditemmodel.h"
#include "songtablemodel.h"
#include <QMainWindow>

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  ~MainWindow();
private slots:
  void selectEntry(const QModelIndex &index);
  void addEntry();
  void open();
  void playSong();
  void pauseSong();
  void seek(int mseconds);

private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  Ui::MainWindow *ui;
  SongTableModel model;
  QStandardItemModel *tableModel; // managed in model; TODO: use inheritance
                                  // instead of composition
  QString currentFile;
  QMediaPlayer *mediaPlayer;
  QMediaPlayer::PlaybackState m_playerState = QMediaPlayer::StoppedState;
  qint64 duration;
};
#endif // MAINWINDOW_H

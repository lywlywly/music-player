#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "lyricshandler.h"
#include "myproxymodel.h"
#include "mytableheader.h"
#include "playercontrolmodel.h"
#include "qmediaplayer.h"
#include "songtablemodel.h"
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

 protected:
  void resizeEvent(QResizeEvent *event) override;

 private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  void statusChanged(QMediaPlayer::MediaStatus status);
  void updateImageSize();
  Ui::MainWindow *ui;
  SongTableModel *model;
  MyProxyModel *proxyModel;
  MyTableHeader *tableHeader;
  QMediaPlayer *mediaPlayer;
  PlayerControlModel *control;
  LyricsHandler *lyricsHandler;
  QPixmap pixmap;
  qint64 duration;
  std::unique_ptr<SongParser> parser{new SongParser()};
};
#endif  // MAINWINDOW_H

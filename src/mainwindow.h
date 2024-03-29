#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>

#include "lyricsloader.h"
#include "lyricsmanager.h"
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
  void newSearchDialog();

 protected:
  void resizeEvent(QResizeEvent *event) override;

 private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  void statusChanged(QMediaPlayer::MediaStatus status);
  void updateImageSize();
  void resetLyricsPanel();
  void updateLyricsPanel(int index);
  void addSongs(QList<Song> songs);
  Ui::MainWindow *ui;
  SongTableModel *model;
  MyProxyModel *proxyModel;
  MyTableHeader *tableHeader;
  QMediaPlayer *mediaPlayer;
  PlayerControlModel *control;
  // TODO: do not inherit QObject, this is a whole utility class outside Qt
  // framework LyricsManager is responsible for providing the right line of
  // lyrics instead
  LyricsLoader *lyricsLoader;
  LyricsManager *lyricsManager;
  QPixmap pixmap;
  qint64 duration;
  int currentLine = -1;
  std::unique_ptr<SongParser> parser{new SongParser()};
  void setUpSplitter();
  void setUpPlaybackActions();
  void setUpSlider();
  void setUpLyricsPanel();
  void setUpTableView();
  void createModels();
  void setUpPlaybackControl();
  void setUpPlaylist();
  void setUpPlayer();
};
#endif  // MAINWINDOW_H

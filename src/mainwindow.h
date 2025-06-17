#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "lyricsloader.h"
#include "lyricsmanager.h"
#include "playbackmanager.h"
#include "playlist.h"
#include "qmediaplayer.h"
#include "songlibrary.h"
#include "songparser.h"
#include <QActionGroup>
#include <QMainWindow>
#include <QMenu>

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

protected:
  void resizeEvent(QResizeEvent *event) override;

private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  void statusChanged(QMediaPlayer::MediaStatus status);
  void updateImageSize();
  void resetLyricsPanel();
  void updateLyricsPanel(int index);
  void setUpImage(MSong song);
  void open();
  void playSong();
  void pauseSong();
  void seek(int mseconds);
  void onCustomContextMenuRequested(const QPoint &pos);
  void addEntry();
  void next();
  void prev();
  void setUpSplitter();
  void setUpPlaybackActions();
  void setUpSlider();
  void setUpLyricsPanel();
  void setUpTableView();
  void createModels();
  void setUpPlaybackControl();
  void setUpPlaylist();
  void setUpPlayer();
  bool eventFilter(QObject *obj, QEvent *event) override;
  // TODO: remove song from current playlist, check if any other playlist
  // references the song, if no, remove from library
  void removeSong();
  Policy string2Policy(QString);
  Ui::MainWindow *ui;
  QMediaPlayer mediaPlayer;
  PlaybackQueue playbackQueue;
  PlaybackManager control;
  SongLibrary songLibrary;
  Playlist playlist{SongStore{songLibrary}, playbackQueue};
  // TODO: do not inherit QObject, this is a whole utility class outside Qt
  // framework LyricsManager is responsible for providing the right line of
  // lyrics instead
  LyricsLoader *lyricsLoader;   // TODO: use value type
  LyricsManager *lyricsManager; // TODO: use value type
  QPixmap pixmap;
  qint64 duration;
  int currentLine = -1;
  std::unique_ptr<SongParser> parser{new SongParser()};
  QActionGroup *orderGroup;
  QMenu contextMenu;
  QAction *playNextAction;
  QAction *playEndAction;
};
#endif // MAINWINDOW_H

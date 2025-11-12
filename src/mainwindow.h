#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "lyricsloader.h"
#include "lyricsmanager.h"
#include "playbackbackendmanager.h"
#include "playbackmanager.h"
#include "playlist.h"
#include "songlibrary.h"
#include <QMainWindow>
#include <QMenu>
#include <QTableView>

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
  void setUpImageAndLyrics(MSong song);
  void open();
  void openFolder();
  void seek(int mseconds);
  void onCustomContextMenuRequested(const QPoint &pos);
  void onTabContextMenuRequested(const QPoint &pos);
  void addEntry();
  void next();
  void prev();
  void play();
  void pause();
  void stop();
  void playSong(const QUrl &);
  void navigateIndex(MSong song, int, int);
  void setUpSplitter();
  void setUpLyricsPanel();
  void setUpTableView(Playlist *, QTableView *);
  void setUpPlaybackManager();
  void setUpCurrentPlaylist();
  void initSettings();
  void initPlaybackBackend();
  void setUpPlaybackBackend();
  void setUpPlaybackActions();
  bool eventFilter(QObject *obj, QEvent *event) override;
  void setUpPlaylist();
  QString getNewPlaylistName();
  void setUpMenuBar();
  // TODO: remove song from current playlist, check if any other playlist
  // references the song, if no, remove from library
  void removeSong();
  void onTabChanged(int);
  Policy string2Policy(QString);
  std::string findPlaylistName(Playlist *);
  int findPlaylistIndex(QString);
  Ui::MainWindow *ui;
  PlaybackBackendManager *backendManager;
  PlaybackQueue playbackQueue_;
  PlaybackManager control;
  SongLibrary songLibrary;
  // TODO: separate playlist manager class
  std::map<std::string, Playlist> playlistMap;
  Playlist *currentPlaylist;
  QTableView *currentTableView;
  LyricsLoader lyricsLoader;   // TODO: use value type
  LyricsManager lyricsManager; // TODO: use value type
  QPixmap pixmap;
  qint64 duration;
  int currentLine = -1;
  QActionGroup *playbackOrderMenuActionGroup;
  QMenu playlistContextMenu;
  QAction *playNextAction;
  QAction *playEndAction;
};
#endif // MAINWINDOW_H

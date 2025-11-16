#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "isystemmediainterface.h"
#include "lyricsloader.h"
#include "lyricsmanager.h"
#include "playbackbackendmanager.h"
#include "playbackmanager.h"
#include "playlist.h"
#include "playlisttabs.h"
#include "songlibrary.h"
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
  void addEntry();
  void next();
  void prev();
  void play();
  void pause();
  void toggle();
  void stop();
  void playSong(const MSong &);
  void navigateIndex(MSong song, int, Playlist *);
  void setUpSplitter();
  void setUpLyricsPanel();
  void setupSystemMediaInterface();
  void initSettings();
  void initPlaybackBackend();
  void setUpPlaybackBackend();
  void setUpPlaybackActions();
  void setUpPlaylist();
  void setUpMenuBar();
  // TODO: remove song from current playlist, check if any other playlist
  // references the song, if no, remove from library
  void removeSong();
  Ui::MainWindow *ui;
  PlaybackBackendManager *backendManager;
  PlaybackQueue playbackQueue_;
  PlaybackManager control;
  SongLibrary songLibrary;
  PlaylistTabs *playlistTabs;
  LyricsLoader lyricsLoader;   // TODO: use value type
  LyricsManager lyricsManager; // TODO: use value type
  QPixmap pixmap;
  qint64 duration;
  QActionGroup *playbackOrderMenuActionGroup;
  ISystemMediaInterface *sysMedia;
};
#endif // MAINWINDOW_H

#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cloudplaystatssynccoordinator.h"
#include "cloudplaystatssyncservice.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "globalcolumnlayoutmanager.h"
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
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  void statusChanged(QMediaPlayer::MediaStatus status);
  void updatePlaybackTimeStatus();
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
  void playSong(const MSong &, int row, Playlist *pl);
  void navigateIndex(MSong song, int, Playlist *);
  void setUpSplitter();
  void setUpLyricsPanel();
  void setupSystemMediaInterface();
  void initSettings();
  void initPlaybackBackend();
  void setUpPlaybackBackend();
  void setUpPlaybackActions();
  void setUpPlaylist();
  void initCloudSync();
  void setUpMenuBar();
  void openLibrarySearchDialog();
  void resetPlayStatsSession(int songPk);
  void maybeCountCompletedPlay();
  void triggerManualCloudRebase();
  void refreshManualCloudRebaseActionEnabled();
  static qint64 unixNowSeconds();
  // TODO: remove song from current playlist, check if any other playlist
  // references the song, if no, remove from library
  void removeSong();
  Ui::MainWindow *ui;
  PlaybackBackendManager *backendManager;
  PlaybackQueue playbackQueue_;
  PlaybackManager control;
  ColumnRegistry columnRegistry_;
  GlobalColumnLayoutManager columnLayoutManager_;
  DatabaseManager databaseManager_;
  SongLibrary songLibrary;
  PlaylistTabs *playlistTabs;
  LyricsLoader lyricsLoader;   // TODO: use value type
  LyricsManager lyricsManager; // TODO: use value type
  QPixmap pixmap;
  QActionGroup *playbackOrderMenuActionGroup;
  ISystemMediaInterface *sysMedia;
  qint64 currentDurationMs_ = 0;
  qint64 currentPositionMs_ = 0;
  int currentTrackPk_ = -1;
  qint64 sessionDurationMs_ = 0;
  qint64 sessionMaxPositionMs_ = 0;
  qint64 sessionListenedMs_ = 0;
  qint64 lastPositionSampleMs_ = -1;
  bool completionCounted_ = false;
  CloudPlayStatsSyncService cloudPlayStatsSyncService_;
  CloudPlayStatsSyncCoordinator cloudPlayStatsSyncCoordinator_;
};
#endif // MAINWINDOW_H

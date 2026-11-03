#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cloudplaystatssynccoordinator.h"
#include "columnregistry.h"
#include "coverartpresenter.h"
#include "databasemanager.h"
#include "globalcolumnlayoutmanager.h"
#include "lyricspresenter.h"
#include "monitoredlibrary.h"
#include "playbackcontroller.h"
#include "playlist.h"
#include "playlistcollection.h"
#include "playlisttabs.h"
#include "songlibrary.h"
#include "systemmediapresenter.h"
#include <QMainWindow>
#include <QString>

class NowPlayingPresenter;
class SettingsDialog;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  MainWindow(QString databaseName, QString connectionName,
             QWidget *parent = nullptr);
  ~MainWindow();

protected:
  bool event(QEvent *event) override;
  void resizeEvent(QResizeEvent *event) override;

private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  // Gives an open Settings dialog (nullptr: none) the current song and
  // runtime symbols for its display expression preview.
  void updateSettingsDisplayPreviewContext(SettingsDialog *dialog);
  // On stop, clears the lyrics and cover.
  void playbackStateChanged(PlaybackQueue::PlaybackStatus status);
  // Selects and scrolls to the row that started playing, if Cursor follows
  // playback is on.
  void navigateIndex(int row, Playlist *);
  void setUpSplitter();
  void initSettings();
  // Creates the audio backend and connects the playback controller's signals
  // to the window.
  void setUpPlayback();
  void initVolumeControl();
  void setUpPlaybackActions();
  void setUpPlaylist();
  void finishSetUpPlaylist();
  void setPlaylistDependentActionsEnabled(bool enabled);
  // Open, Add Folder and Clear Playlist: enabled once the playlists are ready,
  // while a manual playlist is shown.
  void updatePlaylistWriteActionsEnabled();
  // Shows scan progress, then the offline folder count, in the status bar.
  void setUpLibraryScanStatus();
  void initCloudSync();
  void setUpMenuBar();
  void openLibrarySearchDialog();
  void triggerManualCloudRebase();
  void refreshManualCloudRebaseActionEnabled();
  Ui::MainWindow *ui;
  ColumnRegistry columnRegistry_;
  GlobalColumnLayoutManager columnLayoutManager_;
  DatabaseManager databaseManager_;
  SongLibrary songLibrary;
  MonitoredLibrary monitoredLibrary_;
  // Child QObject created after ui->setupUi, so UI widgets (tabs, views,
  // models) are destroyed before the playlists they show.
  PlaylistCollection *playlists_ = nullptr;
  PlaybackController *playback_ = nullptr;
  PlaylistTabs *playlistTabs;
  CoverArtPresenter *coverArt_ = nullptr;
  LyricsPresenter *lyrics_ = nullptr;
  SystemMediaPresenter *systemMedia_ = nullptr;
  NowPlayingPresenter *nowPlaying_ = nullptr;
  QActionGroup *playbackOrderMenuActionGroup;
  CloudPlayStatsSyncCoordinator cloudPlayStatsSyncCoordinator_;
  bool playlistReady_ = false;
};
#endif // MAINWINDOW_H

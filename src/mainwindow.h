#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cloudplaystatssynccoordinator.h"
#include "columnregistry.h"
#include "coverartpresenter.h"
#include "databasemanager.h"
#include "globalcolumnlayoutmanager.h"
#include "isystemmediainterface.h"
#include "lyricspresenter.h"
#include "nowplayingdisplay.h"
#include "playbackcontroller.h"
#include "playlist.h"
#include "playlistcollection.h"
#include "playlisttabs.h"
#include "songlibrary.h"
#include <QMainWindow>
#include <QString>

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
  void bitrateChanged(qint64 bitsPerSecond);
  // The song most recently started (kept after stop), or nullptr
  // before any song has played.
  const MSong *currentSong() const;
  // Gives an open Settings dialog (nullptr: none) the current song and
  // runtime symbols for its display expression preview.
  void updateSettingsDisplayPreviewContext(SettingsDialog *dialog);
  void updatePlaybackTimeStatus();
  void open();
  void openFolder();
  // Updates the status bar, window title and system media; on stop, also
  // clears the now-playing display, lyrics and cover.
  void playbackStateChanged(PlaybackQueue::PlaybackStatus status);
  // Selects and scrolls to the row that started playing, if Cursor follows
  // playback is on.
  void navigateIndex(int row, Playlist *);
  void setUpSplitter();
  void applyDisplayThemeFromSettings();
  void applyDisplayThemeMode(const QString &mode);
  void setupSystemMediaInterface();
  void initSettings();
  // Creates the audio backend and connects the playback controller's signals
  // to the window.
  void setUpPlayback();
  void initVolumeControl();
  void setUpPlaybackActions();
  void setUpPlaylist();
  void finishSetUpPlaylist(SongLibrary::Snapshot &&snapshot);
  void setPlaylistDependentActionsEnabled(bool enabled);
  void initCloudSync();
  void setUpMenuBar();
  void openLibrarySearchDialog();
  void triggerManualCloudRebase();
  void refreshManualCloudRebaseActionEnabled();
  // TODO: remove song from current playlist, check if any other playlist
  // references the song, if no, remove from library
  void removeSong();
  Ui::MainWindow *ui;
  ColumnRegistry columnRegistry_;
  GlobalColumnLayoutManager columnLayoutManager_;
  DatabaseManager databaseManager_;
  SongLibrary songLibrary;
  // Child QObject created after ui->setupUi, so UI widgets (tabs, views,
  // models) are destroyed before the playlists they show.
  PlaylistCollection *playlists_ = nullptr;
  PlaybackController *playback_ = nullptr;
  PlaylistTabs *playlistTabs;
  CoverArtPresenter *coverArt_ = nullptr;
  LyricsPresenter *lyrics_ = nullptr;
  QActionGroup *playbackOrderMenuActionGroup;
  ISystemMediaInterface *sysMedia;
  NowPlayingDisplay nowPlayingDisplay_;
  QString defaultWindowTitle_;
  CloudPlayStatsSyncCoordinator cloudPlayStatsSyncCoordinator_;
  bool playlistReady_ = false;
};
#endif // MAINWINDOW_H

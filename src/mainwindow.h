#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cloudplaystatssynccoordinator.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "globalcolumnlayoutmanager.h"
#include "isystemmediainterface.h"
#include "lyricsmanager.h"
#include "nowplayingdisplay.h"
#include "playbackbackendmanager.h"
#include "playbackmanager.h"
#include "playcompletiontracker.h"
#include "playlist.h"
#include "playlistcollection.h"
#include "playlisttabs.h"
#include "songlibrary.h"
#include <QColor>
#include <QMainWindow>
#include <QString>
#include <QTimer>
#include <unordered_map>

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
  void statusChanged(QMediaPlayer::MediaStatus status);
  // The song most recently started by playSong (kept after stop), or nullptr
  // before any song has played.
  const MSong *currentSong() const;
  // Gives an open Settings dialog (nullptr: none) the current song and
  // runtime symbols for its display expression preview.
  void updateSettingsDisplayPreviewContext(SettingsDialog *dialog);
  void updatePlaybackTimeStatus();
  void updateImageSize();
  void setUpImageAndLyrics(
      const MSong &song,
      const std::unordered_map<std::string, std::string> &remainingFields);
  void clearImageAndLyrics();
  void open();
  void openFolder();
  void seek(int mseconds);
  void next();
  void prev();
  void playRandom();
  void play();
  void pause();
  void toggle();
  void stop();
  void playSong(const MSong &, int row, Playlist *pl);
  // Called when a song cannot be played (missing file, or the backend reports
  // InvalidMedia). Skips to the next song, or stops and reports the file once
  // as many consecutive songs as the current playlist holds have failed.
  void handlePlaybackFailure(const QString &filepath);
  void navigateIndex(int row, Playlist *);
  void setUpSplitter();
  void setUpLyricsPanel();
  void applyLyricsFontFromSettings();
  void applyLyricsHighlightColorFromSettings();
  void setLyricsPanelFont(const QString &fontFamily, int pointSize);
  void setLyricsPanelHighlightColor(const QColor &color);
  void applyDisplayThemeFromSettings();
  void applyDisplayThemeMode(const QString &mode);
  void setupSystemMediaInterface();
  void initSettings();
  void initPlaybackBackend();
  void setUpPlaybackBackend();
  void initVolumeControl();
  void applyVolumeToCurrentBackend();
  void setUpPlaybackActions();
  void setUpPlaylist();
  void finishSetUpPlaylist(SongLibrary::Snapshot &&snapshot);
  void setPlaylistDependentActionsEnabled(bool enabled);
  void initCloudSync();
  void setUpMenuBar();
  void openLibrarySearchDialog();
  // Counts the current song's play once PlayCompletionTracker says so.
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
  // Child QObject created after ui->setupUi, so UI widgets (tabs, views,
  // models) are destroyed before the playlists they show.
  PlaylistCollection *playlists_ = nullptr;
  PlaylistTabs *playlistTabs;
  LyricsManager lyricsManager; // TODO: use value type
  QPixmap pixmap;
  QActionGroup *playbackOrderMenuActionGroup;
  ISystemMediaInterface *sysMedia;
  NowPlayingDisplay nowPlayingDisplay_;
  QString defaultWindowTitle_;
  // The song most recently passed to the backend by playSong, or -1.
  int currentTrackPk_ = -1;
  PlayCompletionTracker playCompletionTracker_;
  CloudPlayStatsSyncCoordinator cloudPlayStatsSyncCoordinator_;
  bool playlistReady_ = false;
  int consecutivePlaybackFailures_ = 0;
  // Runs next() after a failure from the event loop rather than recursively:
  // the next() that hit the failure finishes first, so its selection update
  // does not override the skip's, and a long run of unplayable songs cannot
  // overflow the stack. stop() cancels a pending skip.
  QTimer skipFailedSongTimer_;
};
#endif // MAINWINDOW_H

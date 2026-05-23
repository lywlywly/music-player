#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "cloudplaystatssynccoordinator.h"
#include "columnregistry.h"
#include "databasemanager.h"
#include "globalcolumnlayoutmanager.h"
#include "isystemmediainterface.h"
#include "lyricsmanager.h"
#include "playbackbackendmanager.h"
#include "playbackmanager.h"
#include "playlist.h"
#include "playlisttabs.h"
#include "songlibrary.h"
#include "statusruntimesymboltable.h"
#include <QColor>
#include <QMainWindow>
#include <QString>
#include <unordered_map>

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
  void initStatusBarExpression();
  void initWindowTitleExpression();
  void updateStatusRuntimeSymbols();
  qint64 effectiveBitrateKbps() const;
  QString evaluateStatusBarExpression() const;
  QString evaluateWindowTitleExpression() const;
  void updateOpenSettingsDisplayPreviewContext();
  void updatePlaybackTimeStatus();
  void updateImageSize();
  void setUpImageAndLyrics(
      const MSong &song,
      const std::unordered_map<std::string, std::string> &remainingFields);
  void open();
  void openFolder();
  void seek(int mseconds);
  void addEntry();
  void next();
  void prev();
  void playRandom();
  void play();
  void pause();
  void toggle();
  void stop();
  void playSong(const MSong &, int row, Playlist *pl);
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
  LyricsManager lyricsManager; // TODO: use value type
  QPixmap pixmap;
  QActionGroup *playbackOrderMenuActionGroup;
  ISystemMediaInterface *sysMedia;
  qint64 currentDurationMs_ = 0;
  qint64 currentPositionMs_ = 0;
  qint64 currentBitrateBps_ = 0;
  qint64 currentTagBitrateBps_ = 0;
  bool useTagBitrateForCurrentTrack_ = false;
  StatusRuntimeSymbolTable statusRuntimeSymbols_;
  ExprPtr statusBarExpr_;
  ExprPtr windowTitleExpr_;
  QString defaultWindowTitle_;
  int currentTrackPk_ = -1;
  qint64 sessionDurationMs_ = 0;
  qint64 sessionMaxPositionMs_ = 0;
  qint64 sessionListenedMs_ = 0;
  qint64 lastPositionSampleMs_ = -1;
  bool completionCounted_ = false;
  CloudPlayStatsSyncCoordinator cloudPlayStatsSyncCoordinator_;
  bool playlistReady_ = false;
};
#endif // MAINWINDOW_H

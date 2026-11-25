#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "columnregistry.h"
#include "coverartpresenter.h"
#include "databasemanager.h"
#include "efswfilesystemmonitor.h"
#include "globalcolumnlayoutmanager.h"
#include "layout/panellayout.h"
#include "monitoredlibrary.h"
#include "playbackcontroller.h"
#include "playlist.h"
#include "playlistcollection.h"
#include "playlisttabs.h"
#include "songlibrary.h"
#include "systemmediapresenter.h"
#include <QFuture>
#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <functional>

class NowPlayingPresenter;
class PanelLayoutEditor;
class PanelLayoutView;
class PlaylistSearchDialog;
class PluginManager;
class SettingsDialog;
class SongInfoPresenter;
class SpectrumPresenter;

QT_BEGIN_NAMESPACE
namespace Ui {
class MainWindow;
}
QT_END_NAMESPACE

class MainWindow : public QMainWindow {
  Q_OBJECT

public:
  MainWindow(QWidget *parent = nullptr);
  // The tests' window: its own database and connection, and the folder
  // under which the plugins keep their data.
  MainWindow(QString databaseName, QString connectionName,
             const QString &pluginDataRoot, QWidget *parent = nullptr);
  ~MainWindow();

protected:
  bool event(QEvent *event) override;

private:
  void positionChanged(qint64 progress);
  void durationChanged(qint64 duration);
  // Gives an open Settings dialog (nullptr: none) the current song and
  // runtime symbols for its display expression preview.
  void updateSettingsDisplayPreviewContext(SettingsDialog *dialog);
  // On stop, clears the cover.
  void playbackStateChanged(PlaybackQueue::PlaybackStatus status);
  // Selects and scrolls to the row that started playing, if Cursor follows
  // playback is on.
  void navigateIndex(int row, Playlist *);
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
  // Refreshes the plugins' song fields when their values change.
  void setUpProvidedFields();
  void setUpMenuBar();
  // Registers the toolbar's controls and the panel layout's panels (the
  // plugins' among them), sets up both layouts, and wires View > Reset
  // Layout to reset both (docs/layout-editing.md).
  void setUpLayout();
  // Builds the layout saved at key, or defaultLayout; saves it at key after
  // each change, the view's and the editor's; View > Edit Layout turns the
  // editor on. Returns what resets it to defaultLayout.
  std::function<void()> setUpLayoutView(PanelLayoutView &view,
                                        PanelLayoutEditor &editor,
                                        const char *key,
                                        const PanelLayout &defaultLayout);
  // Adds the plugins' menus to the Playback menu.
  void setUpPluginMenus();
  void openLibrarySearchDialog();
  // Shows the Playlist Search dialog, creating it if none is open. It
  // follows the playlist shown.
  void openPlaylistSearchDialog();
  Ui::MainWindow *ui;
  ColumnRegistry columnRegistry_;
  GlobalColumnLayoutManager columnLayoutManager_;
  DatabaseManager databaseManager_;
  SongLibrary songLibrary;
  EfswFileSystemMonitor fileSystemMonitor_;
  MonitoredLibrary monitoredLibrary_;
  // Child QObject created after ui->setupUi, so UI widgets (tabs, views,
  // models) are destroyed before the playlists they show.
  PlaylistCollection *playlists_ = nullptr;
  PlaybackController *playback_ = nullptr;
  PlaylistTabs *playlistTabs;
  CoverArtPresenter *coverArt_ = nullptr;
  PluginManager *plugins_ = nullptr;
  SongInfoPresenter *songInfo_ = nullptr;
  SpectrumPresenter *spectrum_ = nullptr;
  SystemMediaPresenter *systemMedia_ = nullptr;
  NowPlayingPresenter *nowPlaying_ = nullptr;
  QActionGroup *playbackOrderMenuActionGroup;
  // The startup load's worker; finished once it ran.
  QFuture<void> libraryLoad_;
  bool playlistReady_ = false;
  QPointer<PlaylistSearchDialog> playlistSearchDialog_;
};
#endif // MAINWINDOW_H

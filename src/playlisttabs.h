#ifndef PLAYLISTTABS_H
#define PLAYLISTTABS_H

#include "playbackmanager.h"
#include "playlistcollection.h"
#include <QLineEdit>
#include <QMenu>
#include <QWidget>

class ColumnRegistry;
class GlobalColumnLayoutManager;
class PlaylistTableView;
class QTabWidget;

namespace Ui {
class PlaylistTabs;
}

// UI coordinator for playlist tabs. It creates one PlaylistTableView per tab,
// handles the row and tab context actions, and keeps the active playlist
// synchronized with playback control. Playlist lifecycle and `playlists` table
// writes are delegated to PlaylistCollection; tabs follow its signals. Tab
// identity: each tab stores its DB `playlist_id` in QTabBar::tabData(index),
// and all playlist lookup/removal/reorder persistence uses that id (not tab
// text/index).
class PlaylistTabs : public QWidget {
  Q_OBJECT

public:
  explicit PlaylistTabs(QWidget *parent = nullptr);
  ~PlaylistTabs();
  // collection must already be loaded (PlaylistCollection::loadAll).
  void init(SongLibrary *, PlaybackQueue *, PlaybackManager *, QActionGroup *,
            ColumnRegistry *, GlobalColumnLayoutManager *,
            PlaylistCollection *);
  void setUpPlaylist();
  void onCustomContextMenuRequested(const QPoint &pos);
  void onTabContextMenuRequested(const QPoint &pos);
  QString getNewPlaylistName();
  std::string findPlaylistName(Playlist *);
  int findPlaylistIndex(QString);
  bool removePlaylistTabByIndex(int index);
  QAction *playNextAction() const;
  QAction *playEndAction() const;
  QAction *removeFromPlaylistAction() const;
  QAction *propertiesAction() const;
  QTabWidget *tabWidget() const;
  Playlist *currentPlaylist() const;
  // Clears the current playlist, if there is one.
  void clearCurrentPlaylist();
  void createNewPlaylistTabFromSongIds(const QList<int> &songIds);
  bool renamePlaylistTabByIndex(int index, const QString &newName);
  void navigateIndex(int row, Playlist *pl);
  // PlaylistTableView::firstSelectedRow of the current tab, or -1 without one.
  int firstSelectedRow() const;

signals:
  void doubleClicked(const QModelIndex &index);

private:
  void addPlaylistTab(const QString &playlistName, int playlistId);
  void removePlaylistTab(int playlistId);
  Playlist *playlistForTabIndex(int index);
  // Reads QTabBar::tabData(index) and returns the DB playlist_id for that tab.
  // Returns -1 for invalid index.
  int playlistIdForTabIndex(int index) const;
  // Returns -1 when no tab has this playlist_id.
  int tabIndexForPlaylistId(int playlistId) const;
  // Persists current tab visual order by reading playlist_id from each tab's
  // tabData and passing it to PlaylistCollection::setPlaylistOrder.
  void persistPlaylistTabOrder();
  void onTabChanged(int index);
  void setUpCurrentPlaylist();
  void setUpPlaybackManager();
  void initializePlaylists();
  void beginInlineRename(int index);
  void commitInlineRename();
  void cancelInlineRename();
  Policy selectedPlaybackPolicy() const;
  // Whether row is a row of the current playlist.
  bool isSongRow(int row) const;
  void openPropertiesForRow(int row);
  bool eventFilter(QObject *obj, QEvent *event) override;
  Ui::PlaylistTabs *ui;
  PlaylistTableView *currentTableView;
  QMenu playlistContextMenu;
  QAction *playNextAction_;
  QAction *playEndAction_;
  QAction *removeFromPlaylistAction_ = nullptr;
  QAction *clearPlaylistAction_ = nullptr;
  QAction *propertiesAction_ = nullptr;
  QAction *openContainingFolderAction_ = nullptr;
  SongLibrary *songLibrary;
  PlaylistCollection *collection_ = nullptr;
  PlaybackQueue *playbackQueue_;
  PlaybackManager *control;
  Playlist *currentPlaylist_;
  QActionGroup *playbackOrderMenuActionGroup;
  ColumnRegistry *columnRegistry_ = nullptr;
  GlobalColumnLayoutManager *columnLayoutManager_ = nullptr;
  QLineEdit *tabRenameEditor_ = nullptr;
  int renamingTabIndex_ = -1;
};

#endif // PLAYLISTTABS_H

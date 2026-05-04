#ifndef PLAYLISTTABS_H
#define PLAYLISTTABS_H

#include "globalcolumnlayoutmanager.h"
#include "playbackmanager.h"
#include <QLineEdit>
#include <QMenu>
#include <QTableView>
#include <QWidget>

namespace Ui {
class PlaylistTabs;
}
class DatabaseManager;

// UI coordinator for playlist tabs and table views. It creates playlist models
// per tab, wires header/table interactions, handles tab context actions, and
// keeps the active playlist synchronized with playback control.
// It also owns playlist metadata DB operations (`playlists` table):
// load/repair order, create, delete, and persist tab order.
// Tab identity:
// each tab stores its DB `playlist_id` in QTabBar::tabData(index), and all
// playlist lookup/removal/reorder persistence uses that id (not tab
// text/index).
class PlaylistTabs : public QWidget {
  Q_OBJECT

public:
  explicit PlaylistTabs(QWidget *parent = nullptr);
  ~PlaylistTabs();
  void init(SongLibrary *, PlaybackQueue *, PlaybackManager *, QActionGroup *,
            ColumnRegistry *, GlobalColumnLayoutManager *, DatabaseManager *);
  void setUpPlaylist();
  void onCustomContextMenuRequested(const QPoint &pos);
  void onTabContextMenuRequested(const QPoint &pos);
  QString getNewPlaylistName();
  Policy string2Policy(QString);
  std::string findPlaylistName(Playlist *);
  int findPlaylistIndex(QString);
  bool removePlaylistTabByIndex(int index);
  QAction *playNextAction() const;
  QAction *playEndAction() const;
  QTabWidget *tabWidget() const;
  Playlist *currentPlaylist() const;
  void createNewPlaylistTabFromSongIds(const QList<int> &songIds);
  bool renamePlaylistTabByIndex(int index, const QString &newName);
  void setUpTableView(Playlist *, QTableView *);
  void navigateIndex(MSong, int row, Playlist *pl);
  // Emits row data-changed notifications for this songPk in every playlist
  // model. This is a UI/model refresh signal path and does not mutate song
  // data.
  void notifySongDataChangedInAllPlaylists(int songPk);

signals:
  void doubleClicked(const QModelIndex &index);

private:
  struct PlaylistDefinition {
    int playlistId = -1;
    QString name;
    int tabOrder = -1;
  };
  void createPlaylist(int playlistId, QTableView *tbv);
  void addPlaylistTab(const QString &playlistName, int playlistId);
  void addNewPlaylistTab(const QString &playlistName);
  int insertPlaylistRow(const QString &playlistName, int tabOrder);
  Playlist *playlistForTabIndex(int index);
  // Reads QTabBar::tabData(index) and returns the DB playlist_id for that tab.
  // Returns -1 for invalid index.
  int playlistIdForTabIndex(int index) const;
  // Persists current tab visual order by reading playlist_id from each tab's
  // tabData and writing contiguous tab_order values to DB.
  void persistPlaylistTabOrder();
  // Loads playlists ordered by tab_order (then playlist_id). If DB has no
  // playlist rows, creates the default playlist row (id=1). If tab_order is
  // corrupted (not 0..N-1), repairs order using playlist_id ascending.
  QList<PlaylistDefinition> loadPlaylistsInDisplayOrder() const;
  // Writes contiguous tab_order (0..N-1) for the given playlist_id order.
  bool updatePlaylistTabOrder(const QList<int> &playlistIdsInOrder) const;
  // Deletes one playlist metadata row; playlist_items rows cascade by FK.
  bool deletePlaylistById(int playlistId) const;
  bool updatePlaylistNameById(int playlistId, const QString &name) const;
  void onTabChanged(int index);
  void setUpCurrentPlaylist();
  void setUpPlaybackManager();
  void initializePlaylists();
  void beginInlineRename(int index);
  void commitInlineRename();
  void cancelInlineRename();
  void applyLayoutToTableView(QTableView *tbv);
  void persistVisibleOrder(QTableView *tbv);
  void showHeaderColumnsMenu(QTableView *tbv, const QPoint &pos);
  void refreshPlaylistMetadata(Playlist *playlist);
  bool eventFilter(QObject *obj, QEvent *event) override;
  Ui::PlaylistTabs *ui;
  QTableView *currentTableView;
  QMenu playlistContextMenu;
  QAction *playNextAction_;
  QAction *playEndAction_;
  QAction *clearPlaylistAction_ = nullptr;
  SongLibrary *songLibrary;
  DatabaseManager *databaseManager_ = nullptr;
  PlaybackQueue *playbackQueue_;
  PlaybackManager *control;
  Playlist *currentPlaylist_;
  QActionGroup *playbackOrderMenuActionGroup;
  ColumnRegistry *columnRegistry_ = nullptr;
  GlobalColumnLayoutManager *columnLayoutManager_ = nullptr;
  bool applyingColumnLayout_ = false;
  QLineEdit *tabRenameEditor_ = nullptr;
  int renamingTabIndex_ = -1;
  // TODO: separate playlist manager class
  std::map<int, Playlist> playlistMap;
};

#endif // PLAYLISTTABS_H

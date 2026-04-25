#ifndef PLAYLISTTABS_H
#define PLAYLISTTABS_H

#include "globalcolumnlayoutmanager.h"
#include "playbackmanager.h"
#include <QMenu>
#include <QTableView>
#include <QWidget>

namespace Ui {
class PlaylistTabs;
}

// UI coordinator for playlist tabs and table views. It creates playlist models
// per tab, wires header/table interactions, handles tab context actions, and
// keeps the active playlist synchronized with playback control.
class PlaylistTabs : public QWidget {
  Q_OBJECT

public:
  explicit PlaylistTabs(QWidget *parent = nullptr);
  ~PlaylistTabs();
  void init(SongLibrary *, PlaybackQueue *, PlaybackManager *, QActionGroup *,
            ColumnRegistry *, GlobalColumnLayoutManager *);
  void setUpPlaylist();
  void onCustomContextMenuRequested(const QPoint &pos);
  void onTabContextMenuRequested(const QPoint &pos);
  QString getNewPlaylistName();
  Policy string2Policy(QString);
  std::string findPlaylistName(Playlist *);
  int findPlaylistIndex(QString);
  QAction *playNextAction() const;
  QAction *playEndAction() const;
  QTabWidget *tabWidget() const;
  Playlist *currentPlaylist() const;
  void createNewPlaylistTabFromSongIds(const QList<int> &songIds);
  void setUpTableView(Playlist *, QTableView *);
  void navigateIndex(MSong, int row, Playlist *pl);
  // Emits row data-changed notifications for this filepath in every playlist
  // model. This is a UI/model refresh signal path and does not mutate song
  // data.
  void notifySongDataChangedInAllPlaylists(const std::string &filepath);

signals:
  void doubleClicked(const QModelIndex &index);

private:
  Playlist &createPlaylist(const QString &playlistName, int playlistId,
                           QTableView *tbv);
  Playlist &addPlaylistTab(const QString &playlistName, int playlistId);
  int nextPlaylistId();
  void onTabChanged(int index);
  void setUpCurrentPlaylist();
  void setUpPlaybackManager();
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
  PlaybackQueue *playbackQueue_;
  PlaybackManager *control;
  Playlist *currentPlaylist_;
  QActionGroup *playbackOrderMenuActionGroup;
  ColumnRegistry *columnRegistry_ = nullptr;
  GlobalColumnLayoutManager *columnLayoutManager_ = nullptr;
  bool applyingColumnLayout_ = false;
  int nextPlaylistId_ = 2;
  // TODO: separate playlist manager class
  std::map<std::string, Playlist> playlistMap;
};

#endif // PLAYLISTTABS_H

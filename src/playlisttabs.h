#ifndef PLAYLISTTABS_H
#define PLAYLISTTABS_H

#include "playbackmanager.h"
#include <QMenu>
#include <QTableView>
#include <QWidget>

namespace Ui {
class PlaylistTabs;
}

class PlaylistTabs : public QWidget {
  Q_OBJECT

public:
  explicit PlaylistTabs(QWidget *parent = nullptr);
  ~PlaylistTabs();
  void init(SongLibrary *, PlaybackQueue *, PlaybackManager *, QActionGroup *);
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
  void setUpTableView(Playlist *, QTableView *);
  void navigateIndex(MSong song, int row, Playlist *pl);

signals:
  void doubleClicked(const QModelIndex &index);

private:
  void onTabChanged(int index);
  void setUpCurrentPlaylist();
  void setUpPlaybackManager();
  bool eventFilter(QObject *obj, QEvent *event) override;
  Ui::PlaylistTabs *ui;
  QTableView *currentTableView;
  QMenu playlistContextMenu;
  QAction *playNextAction_;
  QAction *playEndAction_;
  SongLibrary *songLibrary;
  PlaybackQueue *playbackQueue_;
  PlaybackManager *control;
  Playlist *currentPlaylist_;
  QActionGroup *playbackOrderMenuActionGroup;
  // TODO: separate playlist manager class
  std::map<std::string, Playlist> playlistMap;
};

#endif // PLAYLISTTABS_H

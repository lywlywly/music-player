#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "globalcolumnlayoutmanager.h"
#include "playbackqueue.h"
#include "songlibrary.h"
#include "songstore.h"
#include <QAbstractTableModel>

class Playlist : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit Playlist(SongStore &&, PlaybackQueue &, int,
                    GlobalColumnLayoutManager &columnLayoutManager,
                    QObject *parent = nullptr);
  int rowCount(const QModelIndex & = QModelIndex()) const override;
  int columnCount(const QModelIndex & = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  void sortByColumnId(const QString &columnId, int order = 0);
  void sortByField(std::string, int = 0);
  QString columnIdAt(int section) const;
  int songCount() const;
  bool empty() const;
  void addSong(MSong &&);
  void addSongs(std::vector<MSong> &&);
  void removeSong(int);
  void clear();
  const MSong &getSongByPk(int) const;
  const MSong &getSongByIndex(int) const;
  const int getIndexByPk(int) const;
  const int getPkByIndex(int) const;
  // register queue update callback, use when active playlist
  void registerStatusUpdateCallback();
  // unregister queue update callback, use when unregisterStatusUpdateCallback
  // focus
  void unregisterStatusUpdateCallback();
  using SizeChangeCallback = std::function<void(int)>;
  void setSizeChangeCallback(SizeChangeCallback &&) const;
  void unsetSizeChangeCallback() const;
  void setLastPlayed(int newLastPlayed);
  int getLastPlayed() const;
  bool load(QSqlDatabase &);

private:
  const ColumnDefinition *definitionForColumnId(const QString &columnId) const;

  int playlistId;
  SongStore store;
  PlaybackQueue &playbackQueue;
  int lastPlayed = 1;
  mutable SizeChangeCallback sizeChangeCallback;
  GlobalColumnLayoutManager &columnLayoutManager_;
};

#endif // PLAYLIST_H

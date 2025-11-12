#ifndef PLAYLIST_H
#define PLAYLIST_H

#include <QSortFilterProxyModel>

#include "playbackqueue.h"
#include "songlibrary.h"
#include "songstore.h"

static QList<QString> fieldStringList = {"status", "artist", "title",
                                         "path"}; // TODO: per playlist

class Playlist : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit Playlist(SongStore &&, PlaybackQueue &, QObject *parent = nullptr);

  int rowCount(const QModelIndex &parent = QModelIndex()) const override;
  int columnCount(const QModelIndex &parent = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  void sortByField(std::string, int = 0);
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
  const QList<QString> &getFieldStringList() const;
  void toogleSortOrder(int, int);
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

private:
  SongStore store;
  PlaybackQueue &playbackQueue;
  int lastPlayed = 0;
  mutable SizeChangeCallback sizeChangeCallback;
};

#endif // PLAYLIST_H

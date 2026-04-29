#ifndef PLAYLIST_H
#define PLAYLIST_H

#include "globalcolumnlayoutmanager.h"
#include "playbackqueue.h"
#include "songlibrary.h"
#include "songstore.h"
#include <QAbstractTableModel>

// Qt table model for one playlist. It adapts SongStore data to QTableView,
// renders the computed status column from PlaybackQueue, and reacts to global
// column-layout changes.
class Playlist : public QAbstractTableModel {
  Q_OBJECT
public:
  explicit Playlist(SongStore &&, PlaybackQueue &, int initialLastPlayed,
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
  int songCount() const;
  bool empty() const;
  void addSong(MSong &&);
  void addSongByPk(int songPk);
  void addSongs(std::vector<MSong> &&);
  void removeSong(int);
  // Clears all rows from this Qt model and its backing SongStore. For
  // persistent playlists, SongStore also removes DB table `playlist_items`
  // rows; songs remain in DB table `songs`.
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
  // Re-parses metadata for all songs currently in this playlist and writes the
  // refreshed values back to in-memory song data and database tables. The model
  // is reset around the operation. progressCallback is called after each
  // processed song with (current, total), and is intended for progress UI
  // updates (for example, a progress dialog). Assumes every row has a valid
  // non-empty `filepath`; missing/empty filepath is treated as fatal.
  void refreshMetadataFromFiles(
      const std::function<void(int current, int total)> &progressCallback = {});
  // Emits row data-changed notifications for songPk in this
  // model. This refreshes view data without mutating the underlying song map.
  // No-op when the model is empty or songPk is not in the view.
  void emitSongDataChangedBySongPk(int songPk);

private:
  QString columnIdAt(int section) const;

  SongStore store;
  PlaybackQueue &playbackQueue;
  int lastPlayed = 1;
  mutable SizeChangeCallback sizeChangeCallback;
  GlobalColumnLayoutManager &columnLayoutManager_;
};

#endif // PLAYLIST_H

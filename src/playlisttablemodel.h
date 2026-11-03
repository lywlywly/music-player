#ifndef PLAYLISTTABLEMODEL_H
#define PLAYLISTTABLEMODEL_H

#include "globalcolumnlayoutmanager.h"
#include "playbackqueue.h"
#include "playlist.h"
#include <QAbstractTableModel>
#include <QPointer>
#include <vector>

// Qt table model for one Playlist. It owns table presentation (columns, header
// titles, cell text, status glyphs), joins Playlist rows with PlaybackQueue
// status, and reacts to global column-layout changes.
// A reorder of the playlist (sort, move) is a layout change: persistent
// indexes, and so the view's selection and current row, follow their songs.
// Rows can be dragged within the same playlist (kRowsMimeType, which carries
// song pks, so rows changing during the drag do not matter); a drop calls
// Playlist::moveRows.
class PlaylistTableModel : public QAbstractTableModel {
  Q_OBJECT
public:
  // Rows of one playlist, for an internal move.
  static constexpr auto kRowsMimeType = "application/x-myplayer-playlist-rows";

  PlaylistTableModel(Playlist &, PlaybackQueue &,
                     GlobalColumnLayoutManager &columnLayoutManager,
                     QObject *parent = nullptr);
  int rowCount(const QModelIndex & = QModelIndex()) const override;
  int columnCount(const QModelIndex & = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  Qt::ItemFlags flags(const QModelIndex &index) const override;
  Qt::DropActions supportedDropActions() const override;
  QStringList mimeTypes() const override;
  QMimeData *mimeData(const QModelIndexList &indexes) const override;
  // Only kRowsMimeType from this model's playlist, as a move.
  bool canDropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                       int column, const QModelIndex &parent) const override;
  // Moves the dragged songs still in the playlist in front of row (or of
  // parent's row when row is -1, or to the end when both are unset). Returns
  // whether the order changed.
  bool dropMimeData(const QMimeData *data, Qt::DropAction action, int row,
                    int column, const QModelIndex &parent) override;
  Playlist *playlist() const;
  // No-op for unknown or non-sortable columns, otherwise
  // Playlist::sortByField(columnId, order).
  void sortByColumnId(const QString &columnId, int order = 0);

private:
  QString columnIdAt(int section) const;
  // The current rows of the dragged songs still in the playlist, when data is
  // a row drag from this model's playlist; otherwise empty.
  std::vector<int> draggedRows(const QMimeData *data) const;
  void beginReorder();
  void endReorder();

  QPointer<Playlist> playlist_;
  PlaybackQueue &playbackQueue_;
  GlobalColumnLayoutManager &columnLayoutManager_;
  // During a reorder: the persistent indexes and the song pk of each.
  QModelIndexList reorderIndexes_;
  std::vector<int> reorderPks_;
};

#endif // PLAYLISTTABLEMODEL_H

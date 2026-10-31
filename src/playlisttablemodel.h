#ifndef PLAYLISTTABLEMODEL_H
#define PLAYLISTTABLEMODEL_H

#include "globalcolumnlayoutmanager.h"
#include "playbackqueue.h"
#include "playlist.h"
#include <QAbstractTableModel>
#include <QPointer>

// Qt table model for one Playlist. It owns table presentation (columns, header
// titles, cell text, status glyphs), joins Playlist rows with PlaybackQueue
// status, and reacts to global column-layout changes.
class PlaylistTableModel : public QAbstractTableModel {
  Q_OBJECT
public:
  PlaylistTableModel(Playlist &, PlaybackQueue &,
                     GlobalColumnLayoutManager &columnLayoutManager,
                     QObject *parent = nullptr);
  int rowCount(const QModelIndex & = QModelIndex()) const override;
  int columnCount(const QModelIndex & = QModelIndex()) const override;
  QVariant data(const QModelIndex &index,
                int role = Qt::DisplayRole) const override;
  QVariant headerData(int section, Qt::Orientation orientation,
                      int role) const override;
  Playlist *playlist() const;
  // No-op for unknown or non-sortable columns, otherwise
  // Playlist::sortByField(columnId, order).
  void sortByColumnId(const QString &columnId, int order = 0);

private:
  QString columnIdAt(int section) const;

  QPointer<Playlist> playlist_;
  PlaybackQueue &playbackQueue_;
  GlobalColumnLayoutManager &columnLayoutManager_;
};

#endif // PLAYLISTTABLEMODEL_H

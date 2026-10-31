#include "playlisttablemodel.h"

PlaylistTableModel::PlaylistTableModel(
    Playlist &playlist, PlaybackQueue &queue,
    GlobalColumnLayoutManager &columnLayoutManager, QObject *parent)
    : QAbstractTableModel(parent), playlist_{&playlist}, playbackQueue_{queue},
      columnLayoutManager_{columnLayoutManager} {
  connect(&columnLayoutManager_, &GlobalColumnLayoutManager::layoutChanged,
          this, [this]() {
            beginResetModel();
            endResetModel();
          });
  connect(&playlist, &Playlist::aboutToInsertSongs, this,
          [this](int first, int last) {
            beginInsertRows(QModelIndex(), first, last);
          });
  connect(&playlist, &Playlist::songsInserted, this,
          [this](int, int) { endInsertRows(); });
  connect(&playlist, &Playlist::aboutToRemoveSongs, this,
          [this](int first, int last) {
            beginRemoveRows(QModelIndex(), first, last);
          });
  connect(&playlist, &Playlist::songsRemoved, this,
          [this](int, int) { endRemoveRows(); });
  connect(&playlist, &Playlist::aboutToResetSongs, this,
          [this]() { beginResetModel(); });
  connect(&playlist, &Playlist::songsReset, this,
          [this]() { endResetModel(); });
  connect(&playlist, &Playlist::songsReordered, this, [this]() {
    if (rowCount() == 0 || columnCount() == 0) {
      return;
    }
    emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
  });
  connect(&playlist, &Playlist::songDataChanged, this, [this](int row) {
    if (columnCount() == 0) {
      return;
    }
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
  });
  connect(&playbackQueue_, &PlaybackQueue::statusChanged, this,
          [this](int pk, Playlist *pl) {
            if (pl == nullptr || pl != playlist_ || !playlist_->containsPk(pk)) {
              return;
            }

            const int row = playlist_->getIndexByPk(pk);
            const int statusColumn =
                columnLayoutManager_.visibleColumnIds().indexOf("status");
            if (statusColumn < 0) {
              return;
            }

            emit dataChanged(index(row, statusColumn),
                             index(row, statusColumn));
          });
}

int PlaylistTableModel::rowCount(const QModelIndex &) const {
  return playlist_ ? playlist_->songCount() : 0;
}

int PlaylistTableModel::columnCount(const QModelIndex &) const {
  return columnLayoutManager_.visibleColumnIds().size();
}

QVariant PlaylistTableModel::data(const QModelIndex &index, int role) const {
  if (!playlist_ || !index.isValid() || role != Qt::DisplayRole) {
    return {};
  }

  const QString columnId = columnIdAt(index.column());
  if (columnId.isEmpty()) {
    return {};
  }

  if (columnId == "status") {
    const PlaybackQueue::SongStatus status = playbackQueue_.statusOf(
        playlist_->getPkByIndex(index.row()), playlist_);
    switch (status.state) {
    case PlaybackQueue::SongPlaybackState::Playing:
      return QString{"▶"};
    case PlaybackQueue::SongPlaybackState::Paused:
      return QString{"⏸"};
    case PlaybackQueue::SongPlaybackState::Queued:
      return QVariant{status.queuePosition};
    default:
      return {};
    }
  }

  const MSong &song = playlist_->getSongByIndex(index.row());
  const std::string field = columnId.toStdString();
  auto it = song.find(field);
  if (it == song.end()) {
    return {};
  }
  return it->second.display();
}

QVariant PlaylistTableModel::headerData(int section,
                                        Qt::Orientation orientation,
                                        int role) const {
  if (role != Qt::DisplayRole) {
    return {};
  }

  if (orientation == Qt::Orientation::Horizontal) {
    const QString columnId = columnIdAt(section);
    const ColumnDefinition *definition =
        columnLayoutManager_.registry().findColumn(columnId);
    return definition ? QVariant{definition->title} : QVariant{columnId};
  }

  return QString::number(section + 1);
}

Playlist *PlaylistTableModel::playlist() const { return playlist_; }

void PlaylistTableModel::sortByColumnId(const QString &columnId, int order) {
  const ColumnDefinition *definition =
      columnLayoutManager_.registry().findColumn(columnId);
  if (!playlist_ || !definition || !definition->sortable) {
    return;
  }

  playlist_->sortByField(columnId.toStdString(), order);
}

QString PlaylistTableModel::columnIdAt(int section) const {
  if (section < 0) {
    return {};
  }

  const QList<QString> ids = columnLayoutManager_.visibleColumnIds();

  if (section >= ids.size()) {
    return {};
  }

  return ids[section];
}

#include "playlisttablemodel.h"

#include <QDataStream>
#include <QIODevice>
#include <QMimeData>
#include <algorithm>

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
  connect(&playlist, &Playlist::aboutToReorderSongs, this,
          &PlaylistTableModel::beginReorder);
  connect(&playlist, &Playlist::songsReordered, this,
          &PlaylistTableModel::endReorder);
  connect(&playlist, &Playlist::songDataChanged, this, [this](int row) {
    if (columnCount() == 0) {
      return;
    }
    emit dataChanged(index(row, 0), index(row, columnCount() - 1));
  });
  connect(
      &playbackQueue_, &PlaybackQueue::statusChanged, this,
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

        emit dataChanged(index(row, statusColumn), index(row, statusColumn));
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

Qt::ItemFlags PlaylistTableModel::flags(const QModelIndex &index) const {
  const Qt::ItemFlags flags = QAbstractTableModel::flags(index);
  // Drops go between rows (the invalid parent), never onto a row.
  return index.isValid() ? flags | Qt::ItemIsDragEnabled
                         : flags | Qt::ItemIsDropEnabled;
}

Qt::DropActions PlaylistTableModel::supportedDropActions() const {
  return Qt::MoveAction;
}

QStringList PlaylistTableModel::mimeTypes() const {
  return {QString::fromLatin1(kRowsMimeType)};
}

QMimeData *PlaylistTableModel::mimeData(const QModelIndexList &indexes) const {
  if (!playlist_) {
    return nullptr;
  }
  // Song pks, not rows: QDrag::exec runs an event loop, and an autoplaylist's
  // rows can change before the drop (a scan applying its results).
  // The view passes one index per selected cell.
  std::vector<int> rows;
  for (const QModelIndex &index : indexes) {
    if (index.isValid()) {
      rows.push_back(index.row());
    }
  }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  QList<int> songPks;
  songPks.reserve(static_cast<qsizetype>(rows.size()));
  for (int row : rows) {
    songPks.push_back(playlist_->getPkByIndex(row));
  }

  QByteArray encoded;
  QDataStream stream(&encoded, QIODevice::WriteOnly);
  stream << quintptr(playlist_.data()) << songPks;
  auto *data = new QMimeData();
  data->setData(QString::fromLatin1(kRowsMimeType), encoded);
  return data;
}

std::vector<int> PlaylistTableModel::draggedRows(const QMimeData *data) const {
  if (!playlist_ || !data ||
      !data->hasFormat(QString::fromLatin1(kRowsMimeType))) {
    return {};
  }
  QDataStream stream(data->data(QString::fromLatin1(kRowsMimeType)));
  quintptr source = 0;
  QList<int> songPks;
  stream >> source >> songPks;
  if (stream.status() != QDataStream::Ok ||
      source != quintptr(playlist_.data())) {
    return {};
  }
  std::vector<int> rows;
  for (int songPk : std::as_const(songPks)) {
    if (playlist_->containsPk(songPk)) {
      rows.push_back(playlist_->getIndexByPk(songPk));
    }
  }
  return rows;
}

bool PlaylistTableModel::canDropMimeData(const QMimeData *data,
                                         Qt::DropAction action, int, int,
                                         const QModelIndex &) const {
  return action == Qt::MoveAction && !draggedRows(data).empty();
}

bool PlaylistTableModel::dropMimeData(const QMimeData *data,
                                      Qt::DropAction action, int row, int,
                                      const QModelIndex &parent) {
  if (action == Qt::IgnoreAction) {
    return true;
  }
  const std::vector<int> rows = draggedRows(data);
  if (action != Qt::MoveAction || rows.empty()) {
    return false;
  }
  const int destination =
      row >= 0 ? row : (parent.isValid() ? parent.row() : rowCount());
  // The rows move here, so the view's follow-up removeRows for a move must do
  // nothing: this model does not implement removeRows.
  return playlist_->moveRows(rows, destination);
}

// The reorder signals come from playlist_ in pairs, and a reorder keeps the
// same songs.
void PlaylistTableModel::beginReorder() {
  emit layoutAboutToBeChanged({}, QAbstractItemModel::VerticalSortHint);
  reorderIndexes_ = persistentIndexList();
  reorderPks_.clear();
  reorderPks_.reserve(reorderIndexes_.size());
  for (const QModelIndex &index : std::as_const(reorderIndexes_)) {
    reorderPks_.push_back(playlist_->getPkByIndex(index.row()));
  }
}

void PlaylistTableModel::endReorder() {
  // A song is in a playlist at most once, so its pk finds its new row.
  QModelIndexList moved;
  moved.reserve(reorderIndexes_.size());
  for (qsizetype i = 0; i < reorderIndexes_.size(); ++i) {
    moved.push_back(
        index(playlist_->getIndexByPk(reorderPks_[static_cast<size_t>(i)]),
              reorderIndexes_[i].column()));
  }
  changePersistentIndexList(reorderIndexes_, moved);
  reorderIndexes_.clear();
  reorderPks_.clear();
  emit layoutChanged({}, QAbstractItemModel::VerticalSortHint);
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

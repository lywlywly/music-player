#include "playlist.h"
#include "databasemanager.h"
#include <QSqlError>
#include <QSqlQuery>

namespace {
QString songField(const MSong &song, const char *field) {
  auto it = song.find(field);
  if (it == song.end()) {
    return {};
  }
  return QString::fromUtf8(it->second);
}
} // namespace

Playlist::Playlist(SongStore &&st, PlaybackQueue &queue, int pid,
                   GlobalColumnLayoutManager &columnLayoutManager,
                   QObject *parent)
    : QAbstractTableModel(parent), playlistId(pid), store{std::move(st)},
      playbackQueue{queue}, columnLayoutManager_{columnLayoutManager} {
  connect(&columnLayoutManager_, &GlobalColumnLayoutManager::layoutChanged,
          this, [this]() {
            beginResetModel();
            endResetModel();
          });
  load(DatabaseManager::db());
}

int Playlist::rowCount(const QModelIndex &) const { return songCount(); }

int Playlist::columnCount(const QModelIndex &) const {
  return columnLayoutManager_.visibleColumnIds().size();
}

QVariant Playlist::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole) {
    return {};
  }

  const QString columnId = columnIdAt(index.column());
  if (columnId.isEmpty()) {
    return {};
  }

  if (columnId == "status") {
    const int pk = store.getPkByIndex(index.row());
    const auto &[curPk, curPl] = playbackQueue.getCurrentPk();

    if ((pk == curPk) && (curPl == this)) {
      switch (playbackQueue.getStatus()) {
      case PlaybackQueue::PlaybackStatus::Playing:
        return QString{"\u25B6"};
      case PlaybackQueue::PlaybackStatus::Paused:
        return QString{"\u23F8"};
      default:
        return {};
      }
    }

    auto [order, pl] = playbackQueue.getOrder(pk);
    if ((order >= 0) && (pl == this)) {
      return QVariant{order};
    }

    return {};
  }

  const MSong &song = getSongByIndex(index.row());
  if (columnId == "artist") {
    return songField(song, "artist");
  }
  if (columnId == "title") {
    return songField(song, "title");
  }
  if (columnId == "path") {
    return songField(song, "path");
  }

  const std::string dynamicField = columnId.toStdString();
  return songField(song, dynamicField.c_str());
}

QVariant Playlist::headerData(int section, Qt::Orientation orientation,
                              int role) const {
  if (role != Qt::DisplayRole) {
    return {};
  }

  if (orientation == Qt::Orientation::Horizontal) {
    const QString columnId = columnIdAt(section);
    const ColumnDefinition *definition = definitionForColumnId(columnId);
    return definition ? QVariant{definition->title} : QVariant{columnId};
  }

  return QString::number(section + 1);
}

void Playlist::sortByColumnId(const QString &columnId, int order) {
  const ColumnDefinition *definition = definitionForColumnId(columnId);
  if (!definition || !definition->sortable ||
      definition->source != ColumnSource::SongAttribute) {
    return;
  }

  store.sortByField(columnId.toStdString(), order);
  if (rowCount() == 0 || columnCount() == 0) {
    return;
  }

  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
}

void Playlist::sortByField(std::string field, int order) {
  sortByColumnId(QString::fromStdString(field), order);
}

QString Playlist::columnIdAt(int section) const {
  if (section < 0) {
    return {};
  }

  const QList<QString> ids = columnLayoutManager_.visibleColumnIds();

  if (section >= ids.size()) {
    return {};
  }

  return ids[section];
}

int Playlist::songCount() const { return store.songCount(); }

bool Playlist::empty() const { return songCount() == 0; }

void Playlist::addSong(MSong &&s) {
  beginInsertRows(QModelIndex(), songCount(), songCount());
  store.addSong(std::move(s));
  endInsertRows();

  if (sizeChangeCallback)
    sizeChangeCallback(songCount());
}

void Playlist::addSongs(std::vector<MSong> &&items) {
  if (items.empty())
    return;

  beginInsertRows(QModelIndex(), rowCount(),
                  rowCount() + static_cast<int>(items.size()) - 1);

  for (auto &s : items) {
    store.addSong(std::move(s));
  }

  endInsertRows();
  items = {}; // clear the vector as if we moved it because we are taking rvalue
              // reference

  if (sizeChangeCallback)
    sizeChangeCallback(songCount());
}

void Playlist::removeSong(int index) {
  beginRemoveRows(QModelIndex(), index, index);
  store.removeSongByIndex(index);
  endRemoveRows();

  if (sizeChangeCallback)
    sizeChangeCallback(songCount());
}

void Playlist::clear() {
  beginResetModel();
  store.clear();
  endResetModel();

  if (sizeChangeCallback)
    sizeChangeCallback(songCount());
}

const MSong &Playlist::getSongByPk(int pk) const {
  return store.getSongByPk(pk);
}

const MSong &Playlist::getSongByIndex(int i) const {
  return store.getSongByIndex(i);
}

const int Playlist::getIndexByPk(int pk) const {
  return store.getIndexByPk(pk);
}

const int Playlist::getPkByIndex(int i) const { return store.getPkByIndex(i); }

void Playlist::registerStatusUpdateCallback() {
  std::function<void(int, Playlist *)> statusUpdate = [this](int pk,
                                                             Playlist *pl) {
    if (pl != this) {
      return;
    }

    const int row = this->getIndexByPk(pk);
    const int statusColumn =
        columnLayoutManager_.visibleColumnIds().indexOf("status");
    if (statusColumn < 0) {
      return;
    }

    emit dataChanged(index(row, statusColumn), index(row, statusColumn));
  };
  playbackQueue.setStatusUpdateCallback(std::move(statusUpdate));
}

void Playlist::unregisterStatusUpdateCallback() {
  // TODO
}

void Playlist::setSizeChangeCallback(SizeChangeCallback &&cb_) const {
  this->sizeChangeCallback = std::move(cb_);
}

void Playlist::unsetSizeChangeCallback() const {
  this->sizeChangeCallback = nullptr;
}

void Playlist::setLastPlayed(int newLastPlayed) { lastPlayed = newLastPlayed; }

int Playlist::getLastPlayed() const { return lastPlayed; }

bool Playlist::load(QSqlDatabase &db) {
  if (!store.loadAll(db))
    return false;

  QSqlQuery q(db);

  if (!q.exec(R"(
        SELECT playlist_id, name, last_played
        FROM playlists
        WHERE playlist_id=1
    )"))
    qDebug() << q.lastError().text();
  q.next();
  lastPlayed = q.value(2).toInt();

  return true;
}

const ColumnDefinition *
Playlist::definitionForColumnId(const QString &columnId) const {
  return columnLayoutManager_.registry().findColumn(columnId);
}

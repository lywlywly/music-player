#include "playlist.h"

Playlist::Playlist(SongStore &&st, PlaybackQueue &queue, QObject *parent)
    : store{std::move(st)}, playbackQueue{queue} {}

int Playlist::rowCount(const QModelIndex &parent) const { return songCount(); }

int Playlist::columnCount(const QModelIndex &parent) const {
  return getFieldStringList().size();
}

QVariant Playlist::data(const QModelIndex &index, int role) const {
  if (!index.isValid() || role != Qt::DisplayRole)
    return QVariant();

  if (index.column() == 0) {
    int pk = store.getPkByIndex(index.row());
    const auto &[curPk, curPl] = playbackQueue.getCurrentPk();

    if ((pk == curPk) && (curPl == this))
      return QString{"\u25B6"};

    auto [order, pl] = playbackQueue.getOrder(pk);
    if ((order >= 0) && (pl == this)) {
      return QVariant{order};
    }

    return QVariant{};
  }
  if (index.column() == 1) {
    return QString::fromUtf8(getSongByIndex(index.row()).at("artist"));
  }
  if (index.column() == 2) {
    return QString::fromUtf8(getSongByIndex(index.row()).at("title").c_str());
  }
  if (index.column() == 3) {
    return QString::fromUtf8(getSongByIndex(index.row()).at("path").c_str());
  }
  return QVariant{};
}

QVariant Playlist::headerData(int section, Qt::Orientation orientation,
                              int role) const {
  if (role != Qt::DisplayRole)
    return QVariant();
  if (orientation == Qt::Orientation::Horizontal) {
    return fieldStringList[section];
  } else {
    return QString::number(section + 1);
  }
}

void Playlist::sortByField(std::string s, int i) {
  store.sortByField(s, i);
  emit dataChanged(index(0, 0), index(rowCount() - 1, columnCount() - 1));
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

  beginInsertRows(QModelIndex(), rowCount() - 1,
                  rowCount() - 1 + static_cast<int>(items.size()) - 1);

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

const QList<QString> &Playlist::getFieldStringList() const {
  return fieldStringList;
}

void Playlist::toogleSortOrder(int column, int order) {
  sortByField(fieldStringList[column].toStdString(), order);
}

void Playlist::registerStatusUpdateCallback() {
  std::function<void(int, Playlist *)> statusUpdate = [this](int pk,
                                                             Playlist *pl) {
    if (pl != this) {
      return;
    }

    int row = this->getIndexByPk(pk);
    emit dataChanged(index(row, 0), index(row, 0));
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

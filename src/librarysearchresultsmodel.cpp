#include "librarysearchresultsmodel.h"

LibrarySearchResultsModel::LibrarySearchResultsModel(
    SongLibrary &songLibrary, GlobalColumnLayoutManager &columnLayoutManager,
    QObject *parent)
    : QAbstractTableModel(parent), songLibrary_(songLibrary),
      columnLayoutManager_(columnLayoutManager) {
  connect(&columnLayoutManager_, &GlobalColumnLayoutManager::layoutChanged,
          this, [this]() {
            beginResetModel();
            endResetModel();
          });
}

int LibrarySearchResultsModel::rowCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : static_cast<int>(songIds_.size());
}

int LibrarySearchResultsModel::columnCount(const QModelIndex &parent) const {
  return parent.isValid() ? 0 : visibleSongColumnIds().size();
}

QVariant LibrarySearchResultsModel::data(const QModelIndex &index,
                                         int role) const {
  if (!index.isValid() || role != Qt::DisplayRole) {
    return {};
  }

  const QString columnId = columnIdAt(index.column());
  if (columnId.isEmpty()) {
    return {};
  }

  const MSong &song = songLibrary_.getSongByPK(songIds_[index.row()]);
  return songFieldText(song, columnId.toStdString());
}

QVariant LibrarySearchResultsModel::headerData(int section,
                                               Qt::Orientation orientation,
                                               int role) const {
  if (role != Qt::DisplayRole) {
    return {};
  }

  if (orientation == Qt::Horizontal) {
    const QString columnId = columnIdAt(section);
    const ColumnDefinition *definition =
        columnLayoutManager_.registry().findColumn(columnId);
    return definition ? QVariant{definition->title} : QVariant{columnId};
  }

  return QString::number(section + 1);
}

Qt::ItemFlags LibrarySearchResultsModel::flags(const QModelIndex &index) const {
  if (!index.isValid()) {
    return Qt::NoItemFlags;
  }
  return Qt::ItemIsEnabled | Qt::ItemIsSelectable;
}

void LibrarySearchResultsModel::setMatches(std::vector<int> songIds) {
  beginResetModel();
  songIds_ = std::move(songIds);
  endResetModel();
}

void LibrarySearchResultsModel::clear() { setMatches({}); }

QList<QString> LibrarySearchResultsModel::visibleSongColumnIds() const {
  QList<QString> ids;
  for (const QString &id : columnLayoutManager_.visibleColumnIds()) {
    const ColumnDefinition *definition =
        columnLayoutManager_.registry().findColumn(id);
    if (!definition || definition->source != ColumnSource::SongAttribute) {
      continue;
    }
    ids.push_back(id);
  }
  return ids;
}

QString LibrarySearchResultsModel::columnIdAt(int section) const {
  if (section < 0) {
    return {};
  }

  const QList<QString> ids = visibleSongColumnIds();
  if (section >= ids.size()) {
    return {};
  }
  return ids[section];
}

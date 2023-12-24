#include "songtablemodel.h"

#include <QStandardItemModel>
#include <algorithm>

#include "qurl.h"

std::map<Field, QString> fieldToStringMap = {
    {Field::ARTIST, "Artist"},
    {Field::TITLE, "Title"},
    {Field::GENRE, "Genre"},
    {Field::BPM, "BPM"},
    {Field::REPLAY_GAIN, "Replay Gain"},
    {Field::RATING, "Rating"},
    {Field::COMMENT, "Comment"},
    {Field::FILE_PATH, "File Path"},
};

SongTableModel::SongTableModel(QObject* parent) {
  QList<Field> fields = {Field::ARTIST,  Field::TITLE,       Field::GENRE,
                         Field::BPM,     Field::REPLAY_GAIN, Field::RATING,
                         Field::COMMENT, Field::FILE_PATH};
  // playerControlModel.reset(new PlayerControlModel(this));
  columnSize = fields.size();
  std::transform(fields.begin(), fields.end(),
                 std::back_inserter(fieldStringList),
                 [](Field field) { return fieldToStringMap[field]; });
}

int SongTableModel::rowCount(const QModelIndex& /*parent*/) const {
  return songs.size();
}

int SongTableModel::columnCount(const QModelIndex& /*parent*/) const {
  return columnSize;
}

QVariant SongTableModel::data(const QModelIndex& index, int role) const {
  if (role == Qt::DisplayRole) {
    Song song = songs[index.row()];
    QList<QString> itemList = SongTableModel::songToItemList(song);
    // qDebug() << itemList[index.column()];
    return itemList[index.column()];
  }
  return QVariant();
}

void SongTableModel::appendSong(const QUrl songPath) {
  Song song = parser->parseFile(songPath);
  int newRow = rowCount();
  beginInsertRows(QModelIndex(), newRow, newRow);
  songs.append(song);
  endInsertRows();
  emit playlistChanged();
};

QVariant SongTableModel::headerData(int section, Qt::Orientation orientation,
                                    int role) const {
  if (role == Qt::DisplayRole && orientation == Qt::Orientation::Horizontal) {
    return fieldStringList[section];
  }
  return QVariant();
}

QList<QString> SongTableModel::songToItemList(const Song& song) const {
  QList<QString> itemList;
  itemList << song.artist << song.title << song.genre
           << (song.bpm < 0 ? "" : QString::number(song.bpm))
           << (std::isnan(song.replaygain) ? ""
                                           : QString::number(song.replaygain))
           << (song.rating < 0 ? "" : QString::number(song.rating))
           << song.comment << song.filePath;
  return itemList;
}

QUrl SongTableModel::getEntryUrl(const QModelIndex& index) {
  return songs[index.row()].filePath;
}

QUrl SongTableModel::getEntryUrl(int index) { return songs[index].filePath; }

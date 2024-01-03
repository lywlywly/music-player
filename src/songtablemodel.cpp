#include "songtablemodel.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
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
    {Field::STATUS, "Status"}};

SongTableModel::SongTableModel(QObject* parent) {
  QList<Field> fields = {Field::STATUS, Field::ARTIST,  Field::TITLE,
                         Field::GENRE,  Field::BPM,     Field::REPLAY_GAIN,
                         Field::RATING, Field::COMMENT, Field::FILE_PATH};
  // playerControlModel.reset(new PlayerControlModel(this));
  columnSize = fields.size();
  std::transform(fields.begin(), fields.end(),
                 std::back_inserter(fieldStringList),
                 [](Field field) { return fieldToStringMap[field]; });

  // QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
  // db.setDatabaseName("/home/luyao/testdb/mydatabase.db");
  // // Open the database
  // if (!db.open()) {
  //   qDebug() << "Error opening database:" << db.lastError().text();
  //   // return 1;
  // }

  // qDebug() << "succefusslly open";

  // QSqlQuery query0("SELECT name FROM sqlite_master WHERE type='table'");
  // while (query0.next()) {
  //   QString tableName = query0.value(0).toString();
  //   qDebug() << "Table name:" << tableName;
  // }

  // // Perform a query to open the table
  // QSqlQuery query;
  // if (!query.exec("SELECT * FROM songs")) {
  //   qDebug() << "Error executing query:" << query.lastError().text();
  //   // return 1;
  // }

  // // Process the query results
  // QList<QUrl> urls;
  // while (query.next()) {
  //   // Retrieve data from each row
  //   QString value1 = query.value(0).toString();
  //   QString value2 = query.value(1).toString();

  //   // Process the retrieved data
  //   qDebug() << "Value 1:" << value1;
  //   qDebug() << "Value 2:" << value2;
  //   urls.append(value2);
  // }

  // for (auto url : urls) {
  //   qDebug() << url;
  //   appendSong(url);
  // }
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
    if (index.column() == 0) {
      return -100;
    }
    return itemList[index.column() - 1];
  }
  return QVariant();
}

void SongTableModel::save() {}

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

#include "songtablemodel.h"

#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStandardItemModel>
#include <algorithm>
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;
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
  loader.renewHash();
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

void SongTableModel::load() {
  std::vector<std::string> files;

  std::string directoryPath = "/home/luyao/data/Music/New/";

  for (const auto& entry : fs::directory_iterator(directoryPath)) {
    if (fs::is_regular_file(entry.path())) {
      files.push_back(entry.path().string());
    }
  }
  qDebug() << files.size();
  // Print the files
  for (const auto& file : files) {
    qDebug() << file;
  }

  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
  db.setDatabaseName("/home/luyao/testdb/mydatabase.db");
  // Open the database
  if (!db.open()) {
    qDebug() << "Error opening database:" << db.lastError().text();
    // return 1;
  }

  QSqlQuery query;
  QSqlQuery query0("DELETE FROM songs_full");
  for (const auto& file : files) {
    qDebug() << file;
    Song song = parser->parseFile(QString::fromStdString(file));
    query.exec(QString("INSERT INTO songs_full (artist, genre, rating, path) "
                       "VALUES('%1', '%2', '%3', '%4')")
                   .arg(song.artist)
                   .arg(song.genre)
                   .arg(song.rating)
                   .arg(song.filePath));
  }
}

void SongTableModel::save() { loader.saveUrlsToDB(songs); }

void SongTableModel::loadFromUrls() {
  QList<QString> urls = loader.loadUrlsFromDB();
  qDebug() << "get urls from DB";
  for (auto url : urls) {
    qDebug() << url;
    appendSong(url);
  }
  qDebug() << "done";
}

void SongTableModel::clear() {
  beginRemoveRows(QModelIndex(), 0, songs.size() - 1);
  songs.clear();
  endRemoveRows();
}

void SongTableModel::appendSong(const QUrl& songPath) {
  Song song = parser->parseFile(songPath);
  int newRow = rowCount();
  beginInsertRows(QModelIndex(), newRow, newRow);
  songs.append(song);
  endInsertRows();
  emit playlistChanged();
};

void SongTableModel::appendSong(const Song& song) {
  int newRow = rowCount();
  beginInsertRows(QModelIndex(), newRow, newRow);
  songs.append(song);
  endInsertRows();
  emit playlistChanged();
}

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

#include "songloader.h"

#include "filesystemcomparer.h"
#include "qsqldatabase.h"
#include "qsqlerror.h"
#include "qsqlquery.h"
namespace fs = std::filesystem;

SongLoader::SongLoader(QObject* parent) : QObject{parent} {
  QSqlDatabase db = QSqlDatabase::addDatabase("QSQLITE");
  db.setDatabaseName("/home/luyao/testdb/mydatabase.db");
  if (!db.open()) {
    qDebug() << "Error opening database:" << db.lastError().text();
  }
}

QList<QString> SongLoader::loadUrlsFromDB() {
  // QSqlQuery query0("SELECT name FROM sqlite_master WHERE type='table'");
  // while (query0.next()) {
  //   QString tableName = query0.value(0).toString();
  //   qDebug() << "Table name:" << tableName;
  // }

  QSqlQuery query;
  if (!query.exec("SELECT * FROM songs")) {
    qDebug() << "Error executing query:" << query.lastError().text();
    // return 1;
  }

  QList<QString> urls;
  while (query.next()) {
    QString value2 = query.value(1).toString();
    urls.append(value2);
  }

  return urls;
}

QList<Song> SongLoader::loadMetadataFromDB(QString sql) {
  QSqlQuery query;
  QString queryString;
  if (sql.isEmpty()) {
    queryString = "SELECT * FROM songs_full";
  } else {
    queryString = sql;
  }
  if (!query.exec(queryString)) {
    qDebug() << "Error executing query:" << query.lastError().text();
  }

  QList<Song> songs;
  while (query.next()) {
    Song song;
    song.artist = query.value(1).toString();
    song.title = query.value(2).toString();
    song.genre = query.value(3).toString();
    song.rating = query.value(4).toInt();
    song.filePath = query.value(5).toString();
    songs.append(song);
  }

  return songs;
}

void SongLoader::saveUrlsToDB(const QList<QString>&) {}

void SongLoader::saveUrlsToDB(const QList<Song>& songs) {
  QSqlQuery{"DELETE FROM songs"};
  for (Song s : songs) {
    QSqlQuery query{
        QString{"INSERT INTO songs (path) VALUES ('%1')"}.arg(s.filePath)};
  }
}

std::string SongLoader::escapeSingleQuote(const std::string& input) {
  std::string output;
  for (char c : input) {
    if (c == '\'') {
      output += '\'';
    }
    output += c;
  }
  return output;
}

QString SongLoader::escapeSingleQuote(const QString& input) {
  QString output;
  for (const QChar& c : input) {
    if (c == '\'') {
      output += "\'";
    }
    output += c;
  }
  return output;
}

std::map<std::string, std::string> SongLoader::loadSongHash() {
  QSqlQuery query;
  query.exec("SELECT * FROM songs_full");
  std::map<std::string, std::string> state;
  while (query.next()) {
    state[query.value(5).toString().toStdString()] =
        query.value(6).toString().toStdString();
  }

  return state;
}

// insert if path not exist, else update
void SongLoader::insertSongMetadataToDB(const QString& path) {
  Song song = parser.parseFile(path);
  QSqlQuery query;
  ChecksumCalculator checksumCalc;
  std::string sha1 = checksumCalc.calculateHeaderSHA1(path.toStdString(), 1);
  query.exec(
      QString(
          "REPLACE INTO songs_full (artist, title, genre, rating, path, hash) "
          "VALUES('%1', '%2', '%3', '%4', '%5', '%6')")
          .arg(escapeSingleQuote(song.artist))
          .arg(escapeSingleQuote(song.title))
          .arg(escapeSingleQuote(song.genre))
          .arg(song.rating)
          .arg(escapeSingleQuote(song.filePath))
          .arg(QString::fromStdString(sha1)));
}

void SongLoader::removeSongMetadataFromDB(const QString& path) {
  QSqlQuery query;
  query.exec(QString("DELETE FROM songs_full "
                     "WHERE path='%1'")
                 .arg(escapeSingleQuote(path)));
}

void SongLoader::saveHashToDB(const std::map<std::string, std::string> map) {
  QSqlQuery query;
  QSqlQuery query0("DELETE FROM songs_hash");
  for (const auto& pair : map) {
    query.exec(QString("INSERT INTO songs_hash (path, hash) "
                       "VALUES('%1', '%2')")
                   .arg(QString::fromStdString(escapeSingleQuote(pair.first)))
                   .arg(QString::fromStdString(pair.second)));
  }
}

void SongLoader::renewHash() {
  std::string directoryPath = "/home/luyao/Videos/";
  std::map<std::string, std::string> state1 = loadSongHash();

  FileSystemComparer fsComparer;
  std::map<std::string, std::string> state2 =
      fsComparer.getDirectoryState(directoryPath);
  auto [removed, added, changed] = fsComparer.compareTwoStates(state1, state2);

  for (std::string a : removed) {
    removeSongMetadataFromDB(QString::fromStdString(a));
  }
  for (std::string a : added) {
    insertSongMetadataToDB(QString::fromStdString(a));
  }
  for (std::string a : changed) {
    insertSongMetadataToDB(QString::fromStdString(a));
  }
}

void SongLoader::renewMetaDataDB() { QList<Song> songs = loadMetadataFromDB(); }

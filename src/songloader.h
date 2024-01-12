#ifndef SONGLOADER_H
#define SONGLOADER_H

#include <QObject>

#include "songparser.h"

class SongLoader : public QObject {
  Q_OBJECT
 public:
  explicit SongLoader(QObject *parent = nullptr);
  QList<QString> loadUrlsFromDB();
  QList<Song> loadMetadataFromDB(QString sql = "");
  void saveUrlsToDB(const QList<QString> &);
  void saveUrlsToDB(const QList<Song> &);
  void saveSongsMetaDataToDB(const QString &);
  std::map<std::string, std::string> loadSongHash();
  void saveHashToDB(const std::map<std::string, std::string> map);
  void renewHash();
  void renewMetaDataDB();
  void insertSongMetadataToDB(const QString &path);
  void removeSongMetadataFromDB(const QString &path);
signals:
 private:
  std::string escapeSingleQuote(const std::string& input);
  QString escapeSingleQuote(const QString& input);
  SongParser parser;
};

#endif  // SONGLOADER_H

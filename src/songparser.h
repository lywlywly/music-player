#ifndef SONGPARSER_H
#define SONGPARSER_H

#include "qjsonobject.h"
enum class Field {
  ARTIST,
  TITLE,
  GENRE,
  BPM,
  REPLAY_GAIN,
  RATING,
  COMMENT,
  FILE_PATH,
  STATUS,
};
struct Song {
  QString artist;
  QString title;
  QString genre;
  int bpm = -1;
  double replaygain = std::numeric_limits<double>::quiet_NaN();
  int rating = -1;
  QString comment;
  QString filePath;
};
class SongParser {
 public:
  SongParser();
  Song parseFile(QUrl);
  QT_DEPRECATED Song parseFileLegacy(QUrl);
  std::tuple<std::unique_ptr<uchar[]>, int> parseSongCover(QUrl);

 private:
  QJsonValue getValue(const QJsonObject &jsonObject, Field field);
  QVariant getValue(const QMap<QString, QString> &jsonObject, Field field);
  QMap<Field, QList<QString>> fieldMap = {
      {Field::ARTIST, {"artist", "ARTIST", "Artist"}},
      {Field::TITLE, {"title", "TITLE", "Title"}},
      {Field::GENRE, {"genre", "GENRE", "Genre"}},
      {Field::BPM, {"TBPM"}},
      {Field::REPLAY_GAIN, {"replaygain_track_gain"}},
      {Field::RATING, {"Rating"}},
  };
  int idx = 0;
};

#endif  // SONGPARSER_H

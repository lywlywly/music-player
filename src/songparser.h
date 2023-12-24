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
  FILE_PATH
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
 private:
  QJsonValue getValue(const QJsonObject &jsonObject, Field field);
};

#endif  // SONGPARSER_H

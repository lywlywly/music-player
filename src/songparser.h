#ifndef SONGPARSER_H
#define SONGPARSER_H

#include "songlibrary.h"
#include <qurl.h>

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

namespace SongParser {
Song parseFile(QUrl);
MSong parse(const std::string &);
[[deprecated]] Song parseFileLegacy(QUrl);
[[deprecated]] std::tuple<std::unique_ptr<uchar[]>, int> parseSongCover(QUrl);
std::pair<std::vector<uint8_t>, size_t> extractCoverImage(const std::string &);
} // namespace SongParser

#endif // SONGPARSER_H

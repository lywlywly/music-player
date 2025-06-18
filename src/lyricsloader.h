#ifndef LYRICSLOADER_H
#define LYRICSLOADER_H

#include <filesystem>
#include <map>
#include <string>

class LyricsLoader {
public:
  explicit LyricsLoader();
  std::map<int, std::string> getLyrics(std::string_view title,
                                       std::string_view artist);

private:
  std::filesystem::path findFileByTitleAndArtist(std::string_view title,
                                                 std::string_view artist);
};

#endif // LYRICSLOADER_H

#include "lyricsloader.h"
#include "utils.h"

#include <QDebug>
#include <QTextStream>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>

LyricsLoader::LyricsLoader() {}

void testTmp(const std::string &) {}
void testTmp(const std::string &&) = delete;

std::map<int, std::string> LyricsLoader::getLyrics(std::string_view title,
                                                   std::string_view artist) {
  std::ifstream file(findFileByTitleAndArtist(title, artist));
  std::map<int, std::string> lyricsMap;
  std::map<std::string, std::string> metadata;

  if (!file.is_open()) {
    std::cerr << "Could not open file.\n";
    return lyricsMap;
  }

  std::regex patternMetadata(R"(\[([a-zA-Z]+):(.*)\])");
  std::regex patternTime(R"(\[(.*?)\])");
  std::regex patternText(R"(\]([^\[\]]*)$)");

  std::string line;
  while (std::getline(file, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back(); // Remove carriage return from CRLF lines
    }

    std::smatch matchMetadata;
    if (std::regex_search(line, matchMetadata, patternMetadata)) {
      metadata[matchMetadata.str(1)] = matchMetadata.str(2);
      continue;
    }

    std::vector<int> timeStamps;
    for (auto it = std::sregex_iterator(line.begin(), line.end(), patternTime);
         it != std::sregex_iterator(); ++it) {
      std::string t = (*it).str(1); // e.g., "01:23.45"
      try {
        int minutes = std::stoi(t.substr(0, 2));
        int seconds = std::stoi(t.substr(3, 2));
        float fraction = std::stof(t.substr(6));
        int totalMs =
            (minutes * 60 + seconds) * 1000 + static_cast<int>(fraction * 10);
        timeStamps.push_back(totalMs);
      } catch (...) {
        // Handle malformed timestamps gracefully
        continue;
      }
    }

    std::smatch textMatch;
    if (std::regex_search(line, textMatch, patternText)) {
      const std::string &lyricText = textMatch.str(1);
      for (int ms : timeStamps) {
        lyricsMap[ms] = lyricText;
      }
    }
  }

  return lyricsMap;
}

std::filesystem::path
LyricsLoader::findFileByTitleAndArtist(std::string_view title,
                                       std::string_view artist) {
  std::string filename = std::format("{} - {}.lrc", artist, title);
  return util::getHomePath() / "Music" / filename;
}

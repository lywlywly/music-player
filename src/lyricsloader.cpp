#include "lyricsloader.h"
#include "utils.h"

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

namespace {
std::map<int, std::string> parseLyricsStream(std::istream &stream) {
  std::map<int, std::string> lyricsMap;
  std::regex patternMetadata(R"(\[([a-zA-Z]+):(.*)\])");
  std::regex patternTime(R"(\[(.*?)\])");
  std::regex patternText(R"(\]([^\[\]]*)$)");

  std::string line;
  while (std::getline(stream, line)) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }

    std::smatch matchMetadata;
    if (std::regex_search(line, matchMetadata, patternMetadata)) {
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

std::filesystem::path findFileByTitleAndArtist(std::string_view title,
                                               std::string_view artist) {
  std::string filename = std::format("{} - {}.lrc", artist, title);
  return util::getHomePath() / "Music" / filename;
}
} // namespace

std::map<int, std::string> LyricsLoader::getLyrics(std::string_view title,
                                                   std::string_view artist) {
  std::ifstream file(findFileByTitleAndArtist(title, artist));
  if (!file.is_open()) {
    std::cerr << "Could not open lyrics file.\n";
    return {};
  }
  return parseLyricsStream(file);
}

std::map<int, std::string>
LyricsLoader::parseLyricsText(std::string_view rawText) {
  std::istringstream stream{std::string(rawText)};
  return parseLyricsStream(stream);
}

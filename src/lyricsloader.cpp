#include "lyricsloader.h"
#include "utils.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>
#include <stdexcept>

namespace {
// Converts the fractional part of an LRC timestamp to milliseconds by its
// digit count: "5" -> 500, "45" -> 450, "450" -> 450. Throws
// std::invalid_argument when it is not 1-3 digits.
int fractionToMs(const std::string &digits) {
  if (digits.empty() || digits.size() > 3 ||
      !std::all_of(digits.begin(), digits.end(),
                   [](unsigned char c) { return std::isdigit(c); })) {
    throw std::invalid_argument("invalid LRC fraction");
  }
  int ms = std::stoi(digits);
  for (size_t i = digits.size(); i < 3; ++i) {
    ms *= 10;
  }
  return ms;
}

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
        int totalMs =
            (minutes * 60 + seconds) * 1000 + fractionToMs(t.substr(6));
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

// Known issue: on Windows, path(std::string) decodes with the ANSI code page,
// so a non-ASCII (UTF-8) artist or title yields the wrong file name. See
// docs/architecture.md (LyricsLoader / LyricsManager).
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

std::map<int, std::string> LyricsLoader::resolveLyrics(
    const std::unordered_map<std::string, FieldValue> &song,
    const std::unordered_map<std::string, std::string> &remainingFields) {
  std::string lyricsText;
  for (const char *key : {"attr:unsynced_lyrics", "attr:lyrics"}) {
    auto it = song.find(key);
    if (it != song.end() && !it->second.text.empty()) {
      lyricsText = it->second.text;
      break;
    }
  }
  if (lyricsText.empty()) {
    for (const char *key : {"unsynced_lyrics", "lyrics"}) {
      auto it = remainingFields.find(key);
      if (it != remainingFields.end() && !it->second.empty()) {
        lyricsText = it->second;
        break;
      }
    }
  }
  std::map<int, std::string> lyrics;
  if (!lyricsText.empty()) {
    lyrics = parseLyricsText(lyricsText);
  }
  if (lyrics.empty()) {
    lyrics = getLyrics(song.at("title").text, song.at("artist").text);
  }
  return lyrics;
}

#include "lyricsloader.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <initializer_list>
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

// From the environment (USERPROFILE on Windows, HOME elsewhere), which tests
// point at a temporary folder. QDir::homePath() reads the profile folder from
// the user's token on Windows instead.
std::filesystem::path homePath() {
#ifdef _WIN32
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  return home ? std::filesystem::path(home) : std::filesystem::path{};
}

// Known issue: on Windows, path(std::string) decodes with the ANSI code page,
// so a non-ASCII (UTF-8) artist or title yields the wrong file name. See
// docs/plugins.md (Where lyrics come from).
std::filesystem::path findFileByTitleAndArtist(std::string_view title,
                                               std::string_view artist) {
  std::string filename = std::format("{} - {}.lrc", artist, title);
  return homePath() / "Music" / filename;
}

// The first non-empty value of keys in fields, or an empty string.
QString firstNonEmpty(const QHash<QString, QString> &fields,
                      std::initializer_list<const char *> keys) {
  for (const char *key : keys) {
    const QString value = fields.value(QLatin1String(key));
    if (!value.isEmpty()) {
      return value;
    }
  }
  return {};
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

std::map<int, std::string> LyricsLoader::resolveLyrics(const TrackInfo &track) {
  QString lyricsText =
      firstNonEmpty(track.fields, {"attr:unsynced_lyrics", "attr:lyrics"});
  if (lyricsText.isEmpty()) {
    lyricsText = firstNonEmpty(track.tagFields, {"unsynced_lyrics", "lyrics"});
  }
  std::map<int, std::string> lyrics;
  if (!lyricsText.isEmpty()) {
    lyrics = parseLyricsText(lyricsText.toStdString());
  }
  if (lyrics.empty()) {
    lyrics =
        getLyrics(track.fields.value(QStringLiteral("title")).toStdString(),
                  track.fields.value(QStringLiteral("artist")).toStdString());
  }
  return lyrics;
}

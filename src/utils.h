#ifndef UTILS_H
#define UTILS_H

#include <QString>
#include <cctype>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace util {

inline std::filesystem::path getHomePath() {
#ifdef _WIN32
  const char *home = std::getenv("USERPROFILE");
#else
  const char *home = std::getenv("HOME");
#endif
  return home ? std::filesystem::path(home) : std::filesystem::path{};
}

inline bool createDirectory(const std::filesystem::path &path) {
  std::error_code ec;
  if (std::filesystem::exists(path))
    return true;
  return std::filesystem::create_directories(path, ec);
}

inline std::string canonicalizeTagKey(const std::string &rawValue) {
  std::string normalized;
  normalized.reserve(rawValue.size());
  bool prevUnderscore = false;

  for (unsigned char ch : rawValue) {
    if (std::isalnum(ch)) {
      normalized.push_back(static_cast<char>(std::tolower(ch)));
      prevUnderscore = false;
      continue;
    }
    if (!prevUnderscore) {
      normalized.push_back('_');
      prevUnderscore = true;
    }
  }

  while (!normalized.empty() && normalized.front() == '_') {
    normalized.erase(normalized.begin());
  }
  while (!normalized.empty() && normalized.back() == '_') {
    normalized.pop_back();
  }
  return normalized;
}

inline QString canonicalizeTagKey(const QString &rawValue) {
  QString normalized;
  normalized.reserve(rawValue.size());
  bool prevUnderscore = false;

  for (QChar ch : rawValue) {
    if (ch.isLetterOrNumber()) {
      normalized.append(ch.toLower());
      prevUnderscore = false;
      continue;
    }
    if (!prevUnderscore) {
      normalized.append('_');
      prevUnderscore = true;
    }
  }

  while (!normalized.isEmpty() && normalized.at(0) == '_') {
    normalized.remove(0, 1);
  }
  while (!normalized.isEmpty() && normalized.at(normalized.size() - 1) == '_') {
    normalized.chop(1);
  }
  return normalized;
}

} // namespace util

#endif // UTILS_H

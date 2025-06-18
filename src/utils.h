#ifndef UTILS_H
#define UTILS_H

#include <cstdlib>
#include <filesystem>

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

} // namespace util

#endif // UTILS_H

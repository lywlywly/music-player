#ifndef SONGPARSER_H
#define SONGPARSER_H

#include "columnregistry.h"
#include "songlibrary.h"
#include <optional>
#include <unordered_map>

namespace SongParser {
// Returns std::nullopt when the file cannot be opened (missing, unreadable, or
// not a supported audio format). A readable file without tags is not a
// failure.
std::optional<MSong>
parse(const std::string &, const ColumnRegistry &,
      std::unordered_map<std::string, std::string> *remainingFields = nullptr);
bool writeTags(
    const std::string &filepath,
    const std::unordered_map<std::string, std::string> &updatedFields,
    const ColumnRegistry &columnRegistry);
bool isLikelyCbrAudioFile(const std::string &filepath);
std::pair<std::vector<uint8_t>, size_t> extractCoverImage(const std::string &);
} // namespace SongParser

#endif // SONGPARSER_H

#ifndef SONGPARSER_H
#define SONGPARSER_H

#include "columnregistry.h"
#include "songlibrary.h"
#include <unordered_map>

namespace SongParser {
MSong parse(
    const std::string &, const ColumnRegistry &,
    std::unordered_map<std::string, std::string> *remainingFields = nullptr);
std::pair<std::vector<uint8_t>, size_t> extractCoverImage(const std::string &);
} // namespace SongParser

#endif // SONGPARSER_H

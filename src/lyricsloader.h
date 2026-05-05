#ifndef LYRICSLOADER_H
#define LYRICSLOADER_H

#include <map>
#include <string>
#include <string_view>

namespace LyricsLoader {
std::map<int, std::string> getLyrics(std::string_view title,
                                     std::string_view artist);
std::map<int, std::string> parseLyricsText(std::string_view rawText);
} // namespace LyricsLoader

#endif // LYRICSLOADER_H

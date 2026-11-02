#ifndef LYRICSLOADER_H
#define LYRICSLOADER_H

#include "fieldvalue.h"
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace LyricsLoader {
std::map<int, std::string> getLyrics(std::string_view title,
                                     std::string_view artist);
std::map<int, std::string> parseLyricsText(std::string_view rawText);
// The lyrics to show for a song (an MSong): the first non-empty lyrics text of
// the song's `attr:unsynced_lyrics` and `attr:lyrics` fields, then of the
// `unsynced_lyrics` and `lyrics` tag fields in remainingFields (fields from
// the tag refresh that have no column). If none is set or the text has no
// timestamped lines, the `.lrc` file for the song's title and artist
// (getLyrics).
std::map<int, std::string> resolveLyrics(
    const std::unordered_map<std::string, FieldValue> &song,
    const std::unordered_map<std::string, std::string> &remainingFields);
} // namespace LyricsLoader

#endif // LYRICSLOADER_H

#ifndef LYRICSLOADER_H
#define LYRICSLOADER_H

#include "trackinfo.h"
#include <map>
#include <string>
#include <string_view>

namespace LyricsLoader {
std::map<int, std::string> getLyrics(std::string_view title,
                                     std::string_view artist);
std::map<int, std::string> parseLyricsText(std::string_view rawText);
// The lyrics to show for a song: the first non-empty lyrics text of the
// track's `attr:unsynced_lyrics` and `attr:lyrics` fields, then of its
// `unsynced_lyrics` and `lyrics` tag fields. If none is set or the text has no
// timestamped lines, the `.lrc` file for the song's title and artist
// (getLyrics).
std::map<int, std::string> resolveLyrics(const TrackInfo &track);
} // namespace LyricsLoader

#endif // LYRICSLOADER_H

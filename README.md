# myplayer

A cross-platform desktop music player built with Qt, focused on local library playback, lyrics support, and OS media integration.

## Motivation

* foobar2000 is an excellent music player that meets most of my requirements. However, it lacks a dedicated Linux desktop version (although it can be run through Wine), and its macOS version is not fully featured. While DeaDBeeF is a viable alternative, it falls short in terms of available plugins compared to foobar2000, particularly the foo_openlyrics plugin.
* Consequently, I decide to implement a music player that meet my needs. Hope that I can make a player that is both well written and provides good user experience.

## macOS Qt Creator Troubleshooting

Qt Creator may set `DYLD_LIBRARY_PATH` in the Run Environment. On macOS, this can cause system image decode paths used by Media Center artwork/now-playing integration to load Homebrew codec libraries (for example `/opt/homebrew/Cellar/libpng/...`) and lead to runtime crashes.

If issues occur, open Qt Creator `Projects -> Run Settings -> Environment` and remove or unset `DYLD_LIBRARY_PATH` to avoid ImageIO/PNG decode crashes.

## Windows (vcpkg)

If building on Windows with vcpkg, install GStreamer with:

```powershell
.\vcpkg install gstreamer[core,plugins-base,plugins-good,mpg123,flac,ogg,libav,vorbis]
```

Set `GST_PLUGIN_PATH` as:

* Release: `<vcpkg-root>\installed\x64-windows\plugins\gstreamer`
* Debug: `<vcpkg-root>\installed\x64-windows\debug\plugins\gstreamer`

## Library Search Query Syntax

The library search dialog supports a compact query language for filtering songs.

Boolean operators:
* `AND`
* `OR`
* `NOT`

Grouping and precedence:
1. `( ... )`
2. `NOT`
3. `AND`
4. `OR`

Comparison operators:
* `IS` / `=`
* `HAS`
* `IN`
* `<`, `<=`, `>`, `>=`

Value forms:
* scalar: `field IS value`
* list: `field IN [a, b, c]`
* range (inclusive): `field IN [1..5]`

Rules:
* `=` is an alias for `IS`.
* `HAS` expects a scalar value.
* `IN` supports lists for all field types and ranges for numeric/datetime fields.
* Text matching is normalized (case-insensitive style matching).
* `HAS` splits TagLib-style multi-value text on ` / `.
* Typed fields (number/datetime/boolean) use typed comparison; invalid conversions fail parsing.

Examples:

```text
artist IS some artist
genre HAS rock AND NOT title HAS demo
artist IS a AND (genre HAS pop OR genre HAS rock)
genre IN [pop, rock, jazz]
tracknumber >= 2
date IN [2024-01-01..2025-12-31]
```

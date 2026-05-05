# myplayer

## Summary

A cross-platform desktop music player focused on local-library playback, lyrics, and system media integration. It uses C++/Qt 6 with GStreamer or Qt Multimedia backends, stores data in SQLite, and can optionally sync play counts through AWS Lambda + DynamoDB.

## Features

* Multi-playlist local playback with persistent tab order
* Expression-based library search/filter
* ReplayGain (GStreamer backend)
* Lyrics display (embedded tag lyrics on play, with `.lrc` fallback)
* System media integration
* Play stats: `play_count`, `last_played_timestamp`
* Optional UUID-scoped cloud sync for `play_count`

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

## Play Stats and Cloud Sync

Local playback stats:

* `play_count` increments once per play session only after near-end threshold is reached and listened time reaches at least `2/3` of track duration.
* `last_played_timestamp` is updated when playback starts.
* Stats are identity-based (normalized `title|artist|album`), so same-identity songs share counters.

Cloud sync:

* Sync target is `play_count` (UUID-scoped user data).
* Merge rule is `max(local, cloud)` per song identity.
* On local completed-play increment, app pushes `delta=1` asynchronously (best effort).
* On startup, app pulls updates from cloud and reconciles counts.
* If UUID changes and is saved with `OK`, app triggers rebase sync.

## Build and Run

```bash
cd src
cmake -S . -B build
cmake --build build -j
./build/EXEC.app/Contents/MacOS/EXEC   # macOS
```

Build with tests:

```bash
cd src
cmake -S . -B build-tests -DBUILD_TESTING=ON
cmake --build build-tests -j
```

Run tests:

```bash
cd src
ctest --test-dir build-tests --output-on-failure
```

### macOS (Qt Creator troubleshooting)

Qt Creator may set `DYLD_LIBRARY_PATH` in the Run Environment. On macOS, this can cause system image decode paths used by Media Center artwork/now-playing integration to load Homebrew codec libraries (for example `/opt/homebrew/Cellar/libpng/...`) and lead to runtime crashes.

If issues occur, open Qt Creator `Projects -> Run Settings -> Environment` and remove or unset `DYLD_LIBRARY_PATH` to avoid ImageIO/PNG decode crashes.

### Windows (vcpkg)

If building on Windows with vcpkg, install GStreamer with:

```powershell
.\vcpkg install gstreamer[core,plugins-base,plugins-good,mpg123,flac,ogg,libav,vorbis]
```

Set `GST_PLUGIN_PATH` as:

* Release: `<vcpkg-root>\installed\x64-windows\plugins\gstreamer`
* Debug: `<vcpkg-root>\installed\x64-windows\debug\plugins\gstreamer`

## Implementation Notes

Implementation details and architecture live in:

* `docs/architecture.md`

# myplayer

## Summary

A cross-platform desktop music player focused on local-library playback, lyrics, and system media integration. It uses C++/Qt 6 with GStreamer or Qt Multimedia backends, stores data in SQLite, and can optionally sync play counts through AWS Lambda + DynamoDB.

## Features

* Multi-playlist local playback with persistent tab order
* Monitored library: watched folders (Settings > Library) kept in sync with
  the disk, live while the app runs; unmounted drives are shown as offline
  instead of emptying the library
* Autoplaylists: playlists defined by an expression over the monitored
  library, updated as songs change (File > New Autoplaylist, or Save as
  Autoplaylist from Library Search)
* Library Search over the monitored library (Library > Search), with
  expression queries
* Playlist Search over the shown playlist (Edit > Search Playlist, `Ctrl+F` /
  `Cmd+F`), with plain words or an expression
* Song Properties dialog for viewing and editing tags
* ReplayGain (GStreamer backend)
* Lyrics display, a plugin (embedded tag lyrics on play, with `.lrc`
  fallback)
* Properties tab next to Lyrics: the selected song's file name, folder, path,
  size, modified/created times, audio properties, tags and play stats
* Spectrum tab: the playing song's spectrum, 20 bands with peak marks
  (GStreamer backend; the Qt backend only where Qt has its FFmpeg media
  backend)
* Customizable lyrics panel style
* Customizable display expressions (status bar + window title)
* Display theme mode: system default / light / dark
* System media integration
* Play stats: `play_count`, `last_played_timestamp`
* Optional UUID-scoped cloud sync for `play_count`

## Build and Run

TagLib and efsw are built from git submodules, and efsw needs CMake 3.27 or
newer. After cloning:

```bash
git submodule update --init --recursive
```

```bash
cd src
cmake -S . -B build
cmake --build build -j
./build/EXEC.app/Contents/MacOS/EXEC   # macOS
```

Lyrics is a plugin, loaded at startup from `plugins/` next to the executable
(`build/plugins/`), or from `Contents/PlugIns` in the macOS app bundle; the
build puts it there. Without it the app runs with no Lyrics tab. On Windows
the plugin must have the app's build type (Debug or Release).

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

Format code (clang-format, style in `.clang-format`):

```bash
scripts/format.sh            # bash / zsh; add --check to only report
scripts/format.ps1           # PowerShell; add -Check to only report
git clang-format             # only the lines changed in staged files
```

### TagLib source in this repo

By default, this project builds TagLib from the bundled submodule at
`third_party/taglib` (`MYPLAYER_USE_BUNDLED_TAGLIB=ON` in CMake).

This repo uses a [TagLib fork](https://github.com/lywlywly/taglib) submodule and currently prefers
ALAC bitrate computed from MP4 `mdat` payload bytes and duration (average bitrate)
instead of nominal header bitrate.

### macOS (Qt Creator troubleshooting)

Qt Creator may set `DYLD_LIBRARY_PATH` in the Run Environment. On macOS, this can cause system image decode paths used by Media Center artwork/now-playing integration to load Homebrew codec libraries (for example `/opt/homebrew/Cellar/libpng/...`) and lead to runtime crashes.

If issues occur, open Qt Creator `Projects -> Run Settings -> Environment` and remove or unset `DYLD_LIBRARY_PATH` to avoid ImageIO/PNG decode crashes.

### Windows (vcpkg)

Needs Visual Studio with C++ tools and Qt 6 for MSVC. Install GStreamer with
vcpkg:

```powershell
.\vcpkg install gstreamer[core,plugins-base,plugins-good,plugins-bad,mpg123,flac,ogg,libav,vorbis]
```

`plugins-bad` provides the preferred audio output (`wasapi2sink`); without it
the app plays through `directsoundsink`.

Build from `src` in a Developer PowerShell for VS:

```powershell
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg-root>\scripts\buildsystems\vcpkg.cmake `
  -DCMAKE_PREFIX_PATH=<Qt>\msvc2022_64
cmake --build build -j
```

For the tests, configure `build-tests` the same way with `-DBUILD_TESTING=ON`.

To run the app or the tests, put Qt on `PATH` and set `GST_PLUGIN_PATH` (where
the GStreamer plugins are) and `GST_PLUGIN_SCANNER` (GStreamer's helper that
reads new or changed plugins in its own process, so a plugin that crashes
while loading does not take the app down):

```powershell
$env:PATH = "<Qt>\msvc2022_64\bin;$env:PATH"
$env:GST_PLUGIN_PATH = "<vcpkg-root>\installed\x64-windows\plugins\gstreamer"
$env:GST_PLUGIN_SCANNER = "<vcpkg-root>\installed\x64-windows\tools\gstreamer\gst-plugin-scanner.exe"
.\build\EXEC.exe
ctest --test-dir build-tests --output-on-failure
```

For a Debug build, use `-DCMAKE_BUILD_TYPE=Debug` and
`<vcpkg-root>\installed\x64-windows\debug\plugins\gstreamer`, and leave
`GST_PLUGIN_SCANNER` unset: vcpkg's helper is a Release build, which cannot
load the Debug plugins, so GStreamer would mark them all as broken. In Qt
Creator, set these in Projects > Run > Environment. Why the helper matters:
`docs/architecture.md`, "GStreamer plugin registry and scanner".

## Library Search Query Syntax

Library Search, autoplaylists and Playlist Search use a compact query language
for filtering songs. A query must be a condition: `davis` alone is rejected
("Expression must be a condition"). Playlist Search instead searches text that
is not a condition as plain words, each matched against the visible columns,
unless it contains an expression symbol (`=`, `<`, `>`, `(`, `)`, `[`, `]` or
a backtick); then it shows the parse error. A query in double quotes, with no
quotes inside, is always searched as text.

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
* Multi-value tag text is displayed as comma-separated (`a, b, c`); `HAS` matching uses comma-separated values.
* Typed fields (number/datetime/boolean) use typed comparison and reject invalid query literals.
* Interpolated strings use backticks with `${...}` (for example `` `${artist} - ${title}` ``).

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
* On startup, app runs cloud sync immediately (rebase first if `rebase_pending=true`; otherwise incremental pull).
* If UUID changes and is saved with `OK`, app triggers rebase sync.
* Manual cloud rebase is available from the Library menu.

## Implementation Notes

Implementation details and architecture live in:

* `docs/architecture.md`: design principles, classes, startup, playback,
  threading
* `docs/expressions.md`: the expression language, song filters (Library
  Search, autoplaylists, Playlist Search), and the status bar / window title
  expressions
* `docs/monitored-library.md`: watched folders, scans, the file system
  watcher, autoplaylists and Playlist Search
* `docs/cloud-sync.md`: play-count sync design, DynamoDB model, Lambda API

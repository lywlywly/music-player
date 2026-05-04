# Roadmap

Status legend: `✅ done`, `🟡 in progress`, `⬜ not started`

## Features

- ✅ GStreamer backend
- 🟡 ReplayGain
- ✅ Sorting
- ✅ Multiple playlists
  - ✅ Persistent playlist identity (`playlist_id`) and tab order (`tab_order`)
- ⬜ Playlist serialize/deserialize
- 🟡 Database improvements
- 🟡 Lyrics panel polish
- 🟡 Playback policies
  - ✅ Sequential
  - ✅ Random (avoid too-recent items)
  - ⬜ Random by album/artist
- ⬜ Media folder monitoring
- 🟡 Search expressions
  - See [Search Expressions](#search-expressions)
- 🟡 Playback statistics
  - ✅ `play_count` with near-complete gating (seek-to-end alone does not count)
  - ✅ `last_played_timestamp`
  - ✅ Identity-based stats aggregation (shared by normalized title/artist/album)
  - ✅ Cloud sync for playback statistics
- ⬜ CLI tool and headless mode
- 🟡 System media integration
  - ✅ macOS (`MediaPlayer`)
  - ✅ Linux (`MPRIS`)
  - ✅ Windows (`WinRT SMTC`)

## Known Issues

- `QMediaPlayer` seek accuracy on macOS:
  <https://forum.qt.io/topic/107671/qmediaplayer-unable-to-seek-accurately-on-macos>
- Custom slider allows movement when no value is set

## Search Expressions

- Implemented: boolean operators (`AND`/`OR`/`NOT`), parentheses, comparison operators (`IS`/`=`, `HAS`, `IN`, `<`, `<=`, `>`, `>=`), and list/range values.
- Implemented: typed conversion/comparison for numeric, datetime, and boolean fields.
- Implemented: expression values via `IF ... THEN ... ELSE ...` (usable in comparisons).
- Later goal example: `IF encoding IN [flac, alac, pcm, wavpack] THEN "lossless" ELSE "lossy"`.
- Compatibility goal: keep current query syntax valid as a subset while extending the language.

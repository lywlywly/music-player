# Roadmap

Status legend: `✅ done`, `🟡 in progress`, `⬜ not started`

## Features

- ✅ Playback backends
  - ✅ `QMediaPlayer` and `GStreamer` backends
  - ✅ ReplayGain (GStreamer backend)
- ✅ Playback policy and queue
  - ✅ Sequential and Shuffle (tracks)
  - ⬜ Random by album/artist
  - ✅ Playback order persisted in settings
- ✅ Library metadata and expressions
  - ✅ Songs persisted in SQLite (`songs` + related attribute/stat tables)
  - ✅ Built-in + dynamic (`attr:`) + computed fields
  - ✅ Expression-based library filtering/search
  - ✅ Computed fields usable in query/filter
  - See `README.md` (Library Search Query Syntax)
- ✅ Playlist management
  - ✅ Persistent playlist identity (`playlist_id`) and tab order (`tab_order`)
  - ✅ Rename/remove/reorder playlist tabs with DB persistence
  - ✅ Last opened playlist restored on startup
- ✅ Song Properties dialog
  - ✅ View parsed/computed fields and remaining raw tag fields
  - ✅ Edit/add/remove writable tag fields
  - ✅ Buffered save flow via TagLib write + refresh
  - ✅ Multi-value tag edit input using `;` separator
- ✅ Lyrics
  - ✅ Embedded tag lyrics on play, with `.lrc` fallback
  - 🟡 Lyrics panel polish
- ✅ Playback statistics
  - ✅ `play_count` with completion/listen gating
  - ✅ `last_played_timestamp`
  - ✅ Identity-based aggregation (normalized title/artist/album)
  - ✅ UUID-scoped cloud sync (incremental pull + rebase flow)
- ✅ System media integration
  - ✅ macOS (`MediaPlayer`)
  - ✅ Linux (`MPRIS`)
  - ✅ Windows (`WinRT SMTC`)
- ⬜ Media library folder monitoring
- ⬜ CLI tool and headless mode

## Known Issues

- `QMediaPlayer` seek accuracy on macOS:
  <https://forum.qt.io/topic/107671/qmediaplayer-unable-to-seek-accurately-on-macos>
- Custom slider allows movement when no value is set

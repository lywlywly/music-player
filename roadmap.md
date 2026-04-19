# Roadmap

Status legend: `✅ done`, `🟡 in progress`, `⬜ not started`

## Features

- ✅ GStreamer backend
- 🟡 ReplayGain
- ✅ Sorting
- ✅ Multiple playlists
- ⬜ Playlist serialize/deserialize
- 🟡 Database improvements
- 🟡 Lyrics panel polish
- 🟡 Playback policies
  - ✅ Sequential
  - ✅ Random (avoid too-recent items)
  - ⬜ Random by album/artist
- ⬜ Media folder monitoring
- ⬜ Search query patterns
- ⬜ Playback statistics
  - ⬜ Cloud sync for playback statistics
- ⬜ CLI tool and headless mode
- 🟡 System media integration
  - ✅ macOS (`MediaPlayer`)
  - ✅ Linux (`MPRIS`)
  - ✅ Windows (`WinRT SMTC`)

## Known Issues

- `QMediaPlayer` seek accuracy on macOS:
  <https://forum.qt.io/topic/107671/qmediaplayer-unable-to-seek-accurately-on-macos>
- Custom slider allows movement when no value is set

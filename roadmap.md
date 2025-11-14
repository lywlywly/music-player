# Roadmap

- ✅ GStreamer backend
- 🟧 ReplayGain
- ✅ sorting
- ✅ multiple playlists
- ⬜ serialize and deserialize playlists
- ⬜ database
- 🟧 lyrics panel
- 🟧 multiple playback policies
  - ✅ sequential
  - ✅ random with no too recent items
  - ⬜ random album/artist
- ⬜ monitoring media folder
- ⬜ query patterns for media search
- ⬜ playback statistics
  - ⬜ cloud sync
- ⬜ CLI tool and headless mode
- 🟧 system media center integration
  - 🟧 macOS
  - ⬜ linux (MPRIS)
  - ⬜ windows

## Known issues

- `QMediaPlayer` unable to seek accurately on macOS
  - <https://forum.qt.io/topic/107671/qmediaplayer-unable-to-seek-accurately-on-macos>
- Custom slider allows movement when no value is set

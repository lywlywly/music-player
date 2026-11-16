# Roadmap

Status legend: `✅ done`, `🟡 in progress`, `⬜ not started`

## Features

- ✅ Playback backends
  - ✅ `QMediaPlayer` and `GStreamer` backends
  - ✅ ReplayGain (GStreamer backend): Off, Track or Album, in Settings
  - ⬜ Event ordering: a synchronous core with queued edges; see
    `docs/event-ordering.md` (Plan: A synchronous core with queued edges)
    - ⬜ The playback generation, with the slider drag fix and the check on
      plugin seeks
    - ⬜ GStreamer's bus messages in one queued hop (a sync handler)
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
  - ✅ Remove songs from a playlist; songs left in no playlist and not in the
    monitored library leave the library
  - ✅ Autoplaylists (expression over the monitored library, live updates)
  - ✅ Playlist Search (plain text or expression over the shown playlist)
  - ✅ Last opened playlist restored on startup
- ✅ Song Properties dialog
  - ✅ View parsed/computed fields and remaining raw tag fields
  - ✅ Edit/add/remove writable tag fields
  - ✅ Buffered save flow via TagLib write + refresh
  - ✅ Multi-value tag edit input using `;` separator
- ✅ Lyrics
  - ✅ Embedded tag lyrics on play, with `.lrc` fallback
  - ✅ Lyrics panel polish
    - ✅ Click a line to play from it (plugin `play` command), hover
      highlight, scroll hold; see
      `docs/plugins/lyrics.md` (Seeking from a line, hover and the scroll
      hold)
- ✅ Info panel: Lyrics and Properties tabs (top left)
  - ✅ Lyrics panel moved into a tab
  - ✅ Properties tab for the selected song: file name, folder, path, size,
    modified/created times, audio properties, main tags, play stats
  - See `docs/info-panel.md`
- ✅ Spectrum tab (`src/spectrum/` module, library `myspectrum`)
  - ✅ 20 log-spaced bands, 0 to -60 dB, peak hold, own FFT
  - ✅ GStreamer backend; Qt backend only with Qt's FFmpeg media backend
  - See `docs/spectrum.md`
- ✅ Playback statistics
  - ✅ `play_count` with completion/listen gating
  - ✅ `last_played_timestamp`
  - ✅ Identity-based aggregation (normalized title/artist/album)
  - ✅ UUID-scoped cloud sync (incremental pull + rebase flow)
- ✅ System media integration
  - ✅ macOS (`MediaPlayer`)
  - ✅ Linux (`MPRIS`)
  - ✅ Windows (`WinRT SMTC`)
- ✅ Media library folder monitoring
  - ✅ Watched folders with scans (mtime + size) at startup and on demand
  - ✅ Live updates through efsw, debounced into path scans
  - ✅ Offline folders (unmounted drives) release nothing and are polled
  - See `docs/monitored-library.md`
- ✅ Plugins loaded from DLLs, with capabilities (info panel tab, Settings
  page); Lyrics is the first plugin
  - ✅ Plugin API (`src/pluginapi/`) and `PluginManager`
  - ✅ Lyrics plugin (`src/plugins/lyrics/`, `lyricsplugin`)
  - ✅ Plugin management: id and name in metadata, disabling a plugin, a
    Plugins page in Settings
  - ✅ A-B Repeat plugin (`src/plugins/abrepeat/`, `abrepeatplugin`): a
    Playback submenu (`IMenuProvider`) and a deferred plugin `seek`
    (`IPlayerControl`)
  - ⬜ Playback commands for plugins checked against the playback
    generation; see `docs/event-ordering.md` (Plan: A synchronous core with
    queued edges)
  - See `docs/plugins/plugin-system.md`, `docs/plugins/lyrics.md` and
    `docs/plugins/abrepeat.md`
- ✅ Settings dialog navigation tree instead of tabs
  - See `docs/settings-dialog.md`
- ⬜ CLI tool and headless mode

## Architecture

See `docs/architecture.md` (Design Principles).

- ✅ Playlist backend/frontend split (`Playlist`, `PlaylistCollection`,
  `PlaylistTableModel`; playback status owned by `PlaybackQueue`)
- ⬜ Framework-neutral `Event<Args...>` with RAII `Connection` replacing
  backend Qt signals (emit method named `notify`; Qt defines `emit` as a macro)
- ⬜ Playlist ids instead of `Playlist *` in the playback layer
  (`PlaybackManager`, policies, `PlaybackQueue`)
- ⬜ Build split: `MYCore` (Qt6::Core, Qt6::Sql, TagLib) and `MYWidgets`
- ⬜ Qt-free backend internals
- ✅ Persist row removal in `playlist_items`
- ✅ No duplicate songs within a playlist
- ⬜ Persist sort order in `playlist_items`
- ⬜ Batched `playlist_items` writes in one transaction
- ⬜ Pk-based row removal
- ⬜ Move `getNewPlaylistName` from `PlaylistTabs` to `PlaylistCollection`
- ⬜ `SongLibrary` field-existence check without `ColumnDefinition`
  (`ColumnRegistry::findColumn` in song import)

## Known Issues

- `QMediaPlayer` seek accuracy on macOS:
  <https://forum.qt.io/topic/107671/qmediaplayer-unable-to-seek-accurately-on-macos>
- Custom slider allows movement when no value is set
- Deleting a visible `MainWindow` crashes: its members (`ColumnRegistry`,
  `GlobalColumnLayoutManager`) are destroyed before the `QWidget` destructor
  hides the playlist tables, whose headers still read them. The app closes
  its window before that (Qt 6 closes windows on quit)
- GStreamer plugin scans can crash on Windows with heap corruption
  (`0xc0000374`) because of an Intel Graphics driver bug (seen with
  32.0.101.8860). `gstqsv`, GStreamer's Intel video plugin, probes the GPU's
  encoders when it loads, and the driver's `igd11dxva64.dll` writes past the
  end of a Direct3D 11 video decoder's block. It happens in some loads only,
  and only a plugin scan loads `gstqsv`
  - Release build (the app or the tests): harmless, the scan runs in the
    scanner helper (`GST_PLUGIN_SCANNER`), which is the only process that
    can crash
  - Debug build (the app or the tests): it scans in its own process, so a
    scan after the plugins change can crash it
  - The fix is Intel's: a newer driver may have it. See `docs/playback.md`
    (GStreamer plugin registry and scanner)
- `.lrc` fallback misses files with non-ASCII artist/title on Windows
  (UTF-8 file name decoded with the ANSI code page); see
  `docs/plugins/lyrics.md` (Where lyrics come from)
- A seek released in the last milliseconds of a song can be undone by the
  end-of-stream GStreamer posted just before it: the next song starts. See
  `docs/event-ordering.md` (Known races)
- A seek slider drag held past the end of a song seeks the next song on
  release. See `docs/event-ordering.md` (Known races)
- A plugin seek queued just before a song change seeks the next song: with
  A-B Repeat and B in the last 100 ms of a song, the next song starts at A.
  See `docs/event-ordering.md` (Known races)
- Stopping the GLib loop (macOS, Windows) can hang the main thread:
  `PlaybackBackendManager::stopGlibLoop` quits only once the GLib thread
  has set `glibLoop_` and `glibRunning_`, and a `g_main_loop_quit` before
  `g_main_loop_run` starts is lost (the run marks the loop running again);
  either way `join()` waits forever. `glibLoop_` is also a plain pointer
  written on the GLib thread and read on the main thread without
  synchronization, a data race. A hang needs a stop before the new thread
  reaches `g_main_loop_run`: no caller stops that soon today (the earliest
  stop is a Settings change, the app's exit or the end of a
  `tst_gstaudioplayer` test), but code that switches away from a new
  GStreamer manager or destroys it at once would hang. Fix: create the
  loop on the main thread before starting the thread, and quit through a
  source on its context (`g_idle_add` calling `g_main_loop_quit`), which
  runs only once the loop runs. The planned sync handler removes the GLib
  thread, and this bug with it (`docs/playback.md`, Alternative: one hop
  through a sync handler)
- Seeking ahead in a FLAC file without a seek table (GStreamer backend)
  pauses the sound while the accurate seek reads forward from the last
  known frame: near half a second near the end of a high-bitrate file.
  Possible fixes: patch `flacparse` (skip one byte after an invalid
  header), or build vcpkg's GStreamer with GLib's checks compiled in as
  Homebrew does, and seek without `ACCURATE` again. See `docs/playback.md`
  (Seeking)
- A seek on Windows (GStreamer backend) can skip the song with
  `Failed to write to device` (`AUDCLNT_E_BUFFER_TOO_LARGE`): when the
  flushed ring buffer runs out partway through a write, `wasapi2sink` asks
  WASAPI for more silence than fits. Clicking around the seek slider hits
  it after many seeks. The fix is GStreamer's (to report). See
  `docs/playback.md` (Seeking)
- Settings OK with an invalid cloud UUID loses changes: `applySettings`
  runs after the dialog is accepted, so it has closed; Playback, plugin and
  Display settings are already saved, then it shows a warning and returns
  before saving Cloud Sync and Fields. Fix: validate before accepting
  (override `accept`) and keep the dialog open

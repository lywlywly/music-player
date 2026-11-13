# Playback

What plays and in which order, how unplayable songs are skipped, how plays
are counted, the audio backends and their GStreamer setup on Windows, and the
OS media controls. The classes are backend unless marked; how they fit the
rest of the app is in [architecture.md](architecture.md).

## Classes

* `PlaybackQueue`
  * The current song, the songs queued explicitly, and the playback status.
  * `statusOf(songPk, playlist)` returns Playing, Paused or None for the
    current song (its queue position is not consulted), else Queued with its
    position for a song queued from the same playlist. `statusChanged(songPk,
    playlist)` reports changes; `PlaylistTableModel` joins the status with its
    rows.
  * Stores `Playlist` pointers but only compares them, never dereferences
    them. `removePlaylist(playlist)` drops that playlist's current song and
    queued entries; `PlaybackController` calls it on
    `PlaylistCollection::playlistAboutToBeRemoved`, while the id still
    resolves to the `Playlist`.
* `PlaybackManager`
  * Applies the playback policy (`next`, `prev`, `play`, `pause`, `stop`) on
    top of `PlaybackQueue` and the active `Playlist`. Requires
    `setView(Playlist&)` with a valid playlist first.
  * Songs removed from a playlist leave stale ids behind: `next()` skips
    queued songs no longer in their playlist, and the policies treat a current
    song no longer in the playlist as no current song (sequential Next starts
    at the first row, Prev at the last).
* `PlaybackController`
  * Runs playback: the transport commands (play, pause, toggle, stop, next,
    prev, random, play a row, seek), starting a song on the backend, skipping
    unplayable songs and counting completed plays. Owns `PlaybackQueue`,
    `PlaybackManager` and `PlaybackBackendManager`.
  * The first `setBackend` creates the backend. `MainWindow` calls it after
    starting the library load, so creating the backend (about 250 ms for Qt
    Multimedia on a cold start) overlaps the load on the worker thread. A
    backend switch stops playback like `stop()`, and re-applies the volume
    and the `PcmTap` to the new player.
  * Knows nothing about widgets. `MainWindow` gives it the shown playlist and
    the `Playback follows cursor` row as callbacks.
  * Signals: `songAboutToStart` (before the source is set, since the backend
    may report duration and position during `setSource`), `songStarted`,
    `playbackStateChanged` (resume, pause, stop), `movedToRow` (Next, Prev,
    Random), `playbackFailed`, `playCounted`, and the backend's position,
    duration and bitrate. The backend's signals reach the window and the
    presenters only through it, so a backend switch needs no reconnecting.
    `SystemMediaPresenter` connects to the state, seek, position and duration
    signals; `NowPlayingPresenter` ([expressions.md](expressions.md)) to
    `songAboutToStart`, `songStarted`, state, position, duration and bitrate;
    `PluginPlayerHost` ([plugins.md](plugins.md)) to `songStarted`, stop and
    position.
  * Transport commands are ignored until the backend exists and
    `setReady(true)`, which `MainWindow` calls once the library and playlists
    are loaded.
* `PlaybackBackendManager`
  * Owns and switches the backend (`QMediaPlayer` or `GStreamer`).
    `loadPlaybackBackendSetting` / `savePlaybackBackendSetting` read and write
    `playback/backend`, for `MainWindow` and `SettingsDialog`.
  * Runs the GLib main loop for GStreamer on its own `std::thread` on macOS
    and Windows.
  * When GStreamer is the saved backend, `MainWindow` prewarms it: `gst_init`
    on all platforms, and the default output device lookup on macOS. That
    device result is used at startup only; later output refreshes query
    CoreAudio directly.
* `AudioPlayer` (`QtAudioPlayer`, `GstAudioPlayer`)
  * Transport and volume for `PlaybackController`. The volume is a clamped
    integer percentage (`0..100`), mapped to `QAudioOutput::setVolume` or to
    `playbin`'s `volume` property.
  * `setPcmTap(tap)`: decoded audio, before the volume, goes to the tap for
    the spectrum. `GstAudioPlayer` probes `rgvolume`'s src pad; `QtAudioPlayer`
    sets a `QAudioBufferOutput`, which only Qt's FFmpeg media backend fills
    (not the `darwin` one of Homebrew's Qt). The conversion is in
    `audiotaps.{h,cpp}`; see [spectrum.md](spectrum.md).
  * `bitrateChanged(bitsPerSecond)` reports the bitrate while playing.
* Volume slider (`volumeSlider`, frontend): between the playback buttons and
  the seek slider; stored in `playback/volume_percent` (default `100`).

## Stopping and skipping (`PlaybackController`)

* Next with nothing to play (the shown playlist is empty) stops playback.
* Removing the playlist that is playing stops playback before
  `PlaybackQueue::removePlaylist` drops its current song.
* Removing the current song from the library
  (`PlaylistCollection::songsRemovedFromLibrary`) stops playback if it is
  playing or paused, and resets `currentSongPk()` to -1.
* A song that cannot be played is skipped with `next()`: its file is missing
  (checked in `playSong` before the play stats are updated), or the backend
  reports `InvalidMedia` (`GstAudioPlayer` reports bus errors that way too).
  * The previous song's audio stops at once, so its position updates cannot
    reset the failure count and its `EndOfMedia` cannot start a second skip.
  * The skip behaves like the user pressing Next: queued songs first, then
    `Playback follows cursor`, then the playback order; with `Cursor follows
    playback` on, the selection moves to the song that plays.
  * It runs from a zero-delay timer, not recursively, so the `next()` that hit
    the failure finishes (including its `movedToRow`) before the skip starts.
    `stop()` cancels a pending skip.
* After as many consecutive failures as the shown playlist has songs,
  playback stops and `playbackFailed(path)` is emitted; `MainWindow` shows
  `Cannot play file: <path>` in the status bar. The count resets when the
  position advances, or on stop.

## Order (`PlaybackPolicySequential`, `PlaybackPolicyShuffle`)

* `PlaybackPolicyShuffle` sizes its history to half the playlist and follows
  size changes through `Playlist` insert, remove and reset signals; it
  disconnects when switched to another playlist or destroyed.
* Shuffle steps through history entries still in the playlist; otherwise it
  picks a random row whose song is not in the history. Songs are unique per
  playlist and the history holds at most half the rows, so a candidate always
  exists.
* `Playback -> Playback follows cursor` passes the first selected row from
  `MainWindow` (through `PlaybackController`) into `PlaybackManager::next`:
  queued songs still come first, then the selected row is played once if it
  is not already current.
* `Playback -> Cursor follows playback` decides whether `MainWindow` answers
  `PlaybackController::movedToRow` with `PlaylistTabs::navigateIndex`, which
  selects and scrolls to the new row.
* The toolbar's random button is a one-shot jump: it picks a random row of
  the current playlist and calls `playIndex`, without the shuffle history, so
  it can pick the same song again.

## Play statistics

* `last_played_timestamp` is set when playback starts.
* `play_count` goes up once per play session. `PlayCompletionTracker`
  decides when: the song reached `min(90%, end - 5 s)` and two thirds of its
  duration were listened to while playing; a seek adds no listened time. It
  keeps the duration across sessions, since the backend may not report an
  unchanged duration again (replaying the same song).
* `PlaybackController` checks it on position updates and `EndOfMedia`, then
  calls `SongLibrary::incrementPlayCount`, `markCounted()` and
  `PlaylistCollection::notifySongChanged`, and emits `playCounted(songPk)`,
  which `MainWindow` connects to the cloud push ([cloud-sync.md](cloud-sync.md)).
* Stats belong to the song's identity (normalized `title|artist|album`), so
  files with the same identity share them.

## System media controls

* `ISystemMediaInterface` (+ one implementation per platform)
  * The OS media controls and now-playing metadata. No QtWidgets dependency.
  * `createSystemMediaInterface(window)` (`systemmediainterfacefactory.h`)
    picks the implementation; it takes the window because Windows needs its
    native handle. Test builds return `DummySystemMediaInterface` when
    `MYPLAYER_USE_DUMMY_MEDIA_INTERFACE` is set.
* `SystemMediaPresenter` (frontend)
  * Sends the interface's play, pause, toggle, next, previous and seek
    requests to `PlaybackController`, and publishes the controller's state,
    duration, position and seeks to the interface.
  * `MainWindow` calls `showSong` on `PlaybackController::songStarted` to
    publish the title and artist. That clears the system artwork, so it is
    called before `CoverArtPresenter::showSong`, whose `artworkChanged`
    `MainWindow` connects to `setArtwork` ([info-panel.md](info-panel.md)).
  * It does not know `CoverArtPresenter` or `NowPlayingPresenter`;
    `MainWindow` drives all three.

## GStreamer plugin registry and scanner

How `gst_init` learns what the GStreamer plugins provide, and how a plugin
that crashes while loading is kept out of the app. The setup is for Windows
with vcpkg's GStreamer, whose install layout GStreamer's defaults do not
match.

* The registry is GStreamer's cache of what each plugin provides, one file
  per user (on Windows,
  `%LOCALAPPDATA%\Microsoft\Windows\INetCache\gstreamer-1.0\registry.*.bin`).
  `gst_init` reads it, and when the plugin files no longer match it (a file
  added or changed, another `GST_PLUGIN_PATH`) it rescans: it loads each
  plugin and runs its `plugin_init`. The scan loads every installed plugin,
  not only the few the app uses.
* GStreamer is meant to scan in its helper process, `gst-plugin-scanner.exe`,
  which it starts and sends each plugin file to over a named pipe. A plugin
  that crashes while loading then kills only the helper: GStreamer starts a
  new helper and retries the plugin once. If that fails too, it records the
  plugin as blacklisted, with the file's size and modification time, so later
  starts skip it until the file changes. Then it starts another helper and
  goes on.
* Finding the helper:
  1. `GST_PLUGIN_SCANNER_1_0`, else `GST_PLUGIN_SCANNER` (`.exe` is added if
     missing).
  2. Otherwise
     `<libgstreamer's folder>\..\libexec\gstreamer-1.0\gst-plugin-scanner.exe`.
     The `..` steps and `libexec` are GStreamer's build-time `libdir` and
     `libexecdir`.
* Before using the helper, GStreamer runs it once with no arguments and
  expects exit code 1. If the helper can't be started, or exits otherwise,
  GStreamer scans in the app's process, where a crash kills the app. A
  missing helper therefore never shows up as an error.
* The only report of this fallback is
  `GST_WARNING ("Failed starting plugin scanner. Scanning in-process")`.
  vcpkg builds Release with `gst_debug=false`, which defines
  `GST_DISABLE_GST_DEBUG` and compiles `GST_WARNING` to nothing. The visible
  `g_warning` that names `GST_PLUGIN_SCANNER` is a different case: it fires
  only when a helper that did start then fails to load a plugin.
* vcpkg moves the helper from `libexec\gstreamer-1.0` to `tools\gstreamer` and
  deletes `libexec`, so step 2 finds nothing. It moves the plugins too, from
  `lib\gstreamer-1.0` to `plugins\gstreamer`. The README's run commands
  therefore set `GST_PLUGIN_SCANNER`, as they set `GST_PLUGIN_PATH`.
* Source: this behavior is not in GStreamer's documentation (`running.md`
  lists `GST_PLUGIN_PATH` and `GST_REGISTRY_FORK`, not `GST_PLUGIN_SCANNER`).
  The five bullets above, from the helper process to vcpkg's layout, are
  checked against the GStreamer 1.28.7 source that vcpkg builds (its archive
  is in vcpkg's `downloads` folder):
  * `gst/gstpluginloader-win32.c`: `find_helper_bin_location`,
    `gst_plugin_loader_new`, `gst_plugin_loader_retry_pending`.
  * `gst/gstregistry.c`: `gst_registry_scan_plugin_file`, and the size and
    modification-time check in `gst_registry_scan_path_level`.
  * `gst/gstinfo.h`: `GST_WARNING` under `GST_DISABLE_GST_DEBUG`.
  * vcpkg's `ports/gstreamer/portfile.cmake`.

  A newer GStreamer or vcpkg port may change this behavior without notice.
* The crash this guards against: Intel's video plugin, `gstqsv`, probes the
  GPU's codecs in `plugin_init`, and the Intel driver can corrupt the heap
  there ([roadmap.md](../roadmap.md), Known Issues).
* The tests keep their own registry (`src/Tests/test_gstreamer_bootstrap.cpp`:
  a file in the temporary folder, kept between runs as a cache), so they do
  not write to the developer's profile. When the tests and the app have
  different build types, they load different plugins (vcpkg's Debug and
  Release ones), and with one shared registry each switch between running
  them and running the app would make the next start rescan.
* Cost: each Release run configuration (shell, Qt Creator) must set
  `GST_PLUGIN_SCANNER`. vcpkg ships only a Release helper, which cannot load
  the Debug plugins and would blacklist them all, so a Debug build (of the
  app or the tests) leaves it unset and scans in-process, which happens only
  when the plugins change.
* Rejected:
  * Setting `GST_PLUGIN_SCANNER` from the app: the helper is in the build
    machine's vcpkg folder, which the app does not know at runtime and should
    not have built in; the plugin folder is set the same way, by environment.
  * Leaving `gstqsv` out: vcpkg has no option to drop one plugin from
    `plugins-bad`, which the app needs for `wasapi2sink`, and an overlay port
    or deleting the DLL after each install is upkeep for a plugin the helper
    already makes harmless.

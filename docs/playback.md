# Playback

What plays and in which order, how unplayable songs are skipped, the audio
backends, ReplayGain, the GStreamer setup on Windows,
and the OS media controls. The classes are backend unless marked; how they
fit the rest of the app is in [architecture.md](architecture.md).

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
    Random), `seeking` (before the backend is told), `songFinished` (before
    Next), `playbackFailed`, and the backend's position,
    duration and bitrate. The backend's signals reach the window and the
    presenters only through it, so a backend switch needs no reconnecting.
    `SystemMediaPresenter` connects to the state, seek, position and duration
    signals; `NowPlayingPresenter` ([expressions.md](expressions.md)) to
    `songAboutToStart`, `songStarted`, state, position, duration and bitrate;
    `PluginPlayerHost` ([plugins/plugin-system.md](plugins/plugin-system.md)) to
    `songStarted`, stop and position.
  * Transport commands are ignored until the backend exists and
    `setReady(true)`, which `MainWindow` calls once the library and playlists
    are loaded.
* `PlaybackBackendManager`
  * Owns and switches the backend (`QMediaPlayer` or `GStreamer`).
    `loadPlaybackBackendSetting` / `savePlaybackBackendSetting` read and write
    `playback/backend`, for `MainWindow` and `SettingsDialog`.
  * Runs the GLib main loop for GStreamer on its own `std::thread` on macOS
    and Windows, which delivers the bus messages (see "GStreamer bus
    messages").
  * When GStreamer is the saved backend, `MainWindow` prewarms it: `gst_init`
    on all platforms, and the default output device lookup on macOS. That
    device result is used at startup only; later output refreshes query
    CoreAudio directly.
* `AudioPlayer` (`QtAudioPlayer`, `GstAudioPlayer`)
  * Transport and volume for `PlaybackController`. The volume is a clamped
    integer percentage (`0..100`), mapped to `QAudioOutput::setVolume` or to
    `playbin`'s `volume` property.
  * `setPcmTap(tap)`: decoded audio, before the volume, goes to the tap for
    the plugins that show it (the Spectrum). `GstAudioPlayer` probes the src
    pad of its audio filter, after ReplayGain; `QtAudioPlayer` sets a
    `QAudioBufferOutput`, which only Qt's FFmpeg media backend fills (not
    the `darwin` one of Homebrew's Qt). The conversion is in
    `audiotaps.{h,cpp}`; see "Audio tap".
  * `bitrateChanged(bitsPerSecond)` reports the bitrate while playing.
  * `setReplayGainMode(mode)`: see "ReplayGain". `PlaybackController` keeps
    the mode for a new backend, like the volume.
* Volume slider (`volumeSlider`, frontend): a toolbar element, by default
  between the playback buttons and the seek slider
  ([layout-editing.md](layout-editing.md)); stored in
  `playback/volume_percent` (default `100`).

## ReplayGain

* Settings > Playback > ReplayGain, stored in `playback/replaygain`: `off`,
  `track` (the default) or `album` (`loadReplayGainModeSetting` /
  `saveReplayGainModeSetting` in `replaygain.{h,cpp}`, for `MainWindow` and
  `SettingsDialog`). Only `GstAudioPlayer` applies it; Qt Multimedia has no
  ReplayGain.
* `playbin`'s audio filter is a `volume` element, set to the gain.
  GStreamer's demuxers and parsers map each format's ReplayGain fields to
  standard tags (`GST_TAG_TRACK_GAIN`, ...), which a probe on the element's
  sink pad reads on the streaming thread:
  * stream-start, the first event of each song: clears the stored tags;
  * a tag event: adds its values to them (`ReplayGainTags::merge`);
  * after either, the gain for the mode is set on the element.

  The events pass unchanged.
* `setReplayGainMode` recomputes the gain from the stored tags and sets it,
  so every switch, Off included, applies to the song playing. A mutex guards
  the stored tags, which the probe writes and the UI thread reads.
* A switch is heard about 1.2 s later on Windows, in each direction.
  `playbin` puts its audio filter before its audio queue (`aqueue`, up to
  1 s of audio, `gstplaysink.c`), and the sink buffers more (`wasapi2sink`:
  200 ms, its `buffer-time`), so that much audio already has the old gain.
  Kept: the mode is changed rarely, and each way to shorten the delay costs
  more than the delay:
  * the gain in `playbin`'s `volume` (with the slider's), after the queue:
    `wasapi2sink`'s volume stops at 1.0, which cuts positive gains, and the
    plugins' audio tap would read the audio before ReplayGain;
  * the gain element in a sink bin before the sink: `playbin` takes a
    volume element in the sink bin for the slider, which could be ours;
  * a smaller `aqueue`, set through `element-setup`: depends on `playbin`'s
    internal element name, and less buffering risks dropouts;
  * a flushing seek on a switch: an audible gap.
* `ReplayGainTags` (`replaygain.{h,cpp}`, no GStreamer) holds the rules of
  GStreamer's `rgvolume` (`gstrgvolume.c`, 1.28.7), with 0 dB pre-amp,
  fallback gain and headroom:
  * Track uses the track gain, Album the album gain; each falls back to the
    other. Without either, 0 dB.
  * A gain uses its own peak; a missing peak counts as full scale. The gain
    is cut so the peak reaches full scale at most, so a positive gain
    without a peak tag gives 0 dB.
  * The gain is at most +20 dB, the `volume` element's maximum.
  * R128 gains (Opus) take priority, +5 dB, without the event's peak. A
    positive reference level (dB SPL) moves the event's gains to 89 dB SPL;
    one of 0 or below (LUFS) does not.
  * Gains outside ±60 dB and peaks of 0 or below are ignored; peaks above
    1.0 are clamped.

  Off gives 0 dB.
* Where it differs from `rgvolume`, with no audible effect in this app:
  * `rgvolume` clears its tags when it goes from READY to PAUSED and at the
    end of the stream; the probe clears them at stream-start. Each song
    starts with `setSource`, which goes through READY, so both clear before
    the song's tags.
  * An ignored value keeps the stored one; `rgvolume` overwrites its stored
    value with it.
  * `rgvolume` removes the gain tags from the events it passes on; nothing
    after the filter reads them.
* Rejected:
  * `rgvolume` as the filter. It has no Off, and it keeps the gain tags it
    has read until its stream resets. Off would have to hide the tags from
    it, so a switch to Off would wait for the next song, and one from Off
    until the decoder sends the tags again, which it may never do.
  * `rgvolume` with a second `volume` element that cancels its gain for Off:
    a workaround that applies a gain to undo it.
* Cost: the app keeps `rgvolume`'s rules itself, and a change to them in
  GStreamer does not reach it.
* Tests: `tst_replaygain`, each rule; `tst_gstaudioplayer`, a tagged tone
  read at the audio tap in each mode, switched while it plays, and an
  untagged song after it.

## Audio tap (`PcmTap`, `audiotaps`)

The players copy the decoded audio into one tap, for the plugins that show it
([plugins/plugin-system.md](plugins/plugin-system.md), "Audio for plugins",
which owns the tap and sets it while a plugin wants audio).

### `PcmTap`

Qt-free.

* `push(data, frames, channels, format, sampleRate, startUs)`: any thread.
  Interleaved little-endian frames in `UInt8`, `Int8`, `Int16`, `Int24`
  (packed, 3 bytes), `Int32`, `Float32` or `Float64`, converted to float in
  [-1, 1] and mixed to mono (the mean of the channels). `startUs` is the
  stream time of the first frame.
* Audio is kept as one run of samples. A push that does not follow on (more
  than 50 ms off, or a different sample rate) starts a new run; this covers
  buffers still in flight after a seek. Samples more than 2 s before the
  newest are dropped.
* `read(endUs, out)`: the `out.size()` samples ending at stream time `endUs`
  (rounded to the nearest sample), with zeros where there is no audio.
  Returns the sample rate, or 0 when no audio overlaps.
* `clear()`; the players call it on a new source, a seek and stop.
* A mutex guards the samples: the GStreamer streaming thread pushes, the GUI
  thread reads.

### The players' taps

* `AudioPlayer`: `virtual void setPcmTap(std::shared_ptr<PcmTap>)` and
  `pcmTap()`. The base stores the tap behind a mutex, because the GStreamer
  streaming thread reads it. Protected `clearPcmTap()`. With no tap, no
  audio is copied: the GStreamer probe only finds no tap, and the Qt player
  has no buffer output set.
* `GstAudioPlayer`: a buffer probe (`onPcmProbe`) on the `src` pad of the
  ReplayGain `volume` element, `playbin`'s audio filter (see "ReplayGain"),
  installed with the pipeline. That is after ReplayGain and before the volume
  slider, which playsink applies later: the audio is there at volume 0. It is not
  removed: teardown sets the pipeline to NULL first, and the probe goes with
  the element. `setSource`, `setPosition` and `stop` clear the tap.
* `QtAudioPlayer`: one `QAudioBufferOutput` (no requested format, so buffers
  come in the decoded format), set on the `QMediaPlayer` only while a tap is
  set, connected with `Qt::DirectConnection`: the push runs in whatever
  thread sends the buffer. `setSource`, `setPosition` and `stop` clear the
  tap. With the `darwin` backend no buffer arrives
  ([plugins/spectrum.md](plugins/spectrum.md), "Summary").
* `audiotaps.{h,cpp}`, free functions, so they are tested without an audio
  device:
  * `pushGstBuffer(pad, buffer, tap)`: reads `format`, `rate`, `channels`
    and `layout` from the pad's current caps with core `GstStructure` calls
    (no `gstreamer-audio-1.0`). It takes every format the `volume` element
    can output: `F32LE`, `F64LE`, `S8`, `S16LE`, `S24LE`, `S32LE`,
    interleaved only (the supported platforms are little-endian); a 24-bit
    FLAC arrives as `S24LE`. The buffer PTS becomes stream time through the
    pad's sticky segment (`gst_segment_to_stream_time`), which is what the
    position query reports.
  * `pushAudioBuffer(buffer, tap)`: maps `QAudioFormat` to the tap's
    formats; `QAudioBuffer::startTime()` is the stream time. Buffers without
    one are skipped.
* `PlaybackController::setPcmTap(tap)`: kept, set on the current player,
  and set again on the new player in `setBackend` (as the volume is), so a
  backend switch keeps the audio.
* Tests: `tst_audiotaps`, `PcmTap` (formats, reads at a time with zero fill,
  runs and dropping old audio) and every format the `volume` element can
  output through a real pipeline and a `QAudioBuffer`, and what is skipped;
  `tst_playbackcontroller`, the tap moving to a new player;
  `tst_gstaudioplayer`, the real `GstAudioPlayer` filling the tap at the
  reported position, after a seek, and cleared on stop (skipped without an
  audio output).

## Stopping and skipping (`PlaybackController`)

* Next with nothing to play (the shown playlist is empty) stops playback.
* Removing the playlist that is playing stops playback before
  `PlaybackQueue::removePlaylist` drops its current song.
* Removing the current song from the library
  (`PlaylistCollection::songsRemovedFromLibrary`) stops playback if it is
  playing or paused, and resets `currentSongPk()` to -1.
* A song that cannot be played is skipped with `next()`: its file is missing
  (checked in `playSong` before `songStarted`), or the backend
  reports `InvalidMedia` (`GstAudioPlayer` reports bus errors that way too).
  * The previous song's audio stops at once, so its position updates cannot
    reset the failure count and its `EndOfMedia` cannot start a second skip.
  * The skip behaves like the user pressing Next: queued songs first, then
    `Playback follows cursor`, then the playback order; with `Cursor follows
    playback` on, the selection moves to the song that plays.
  * It runs from a zero-delay timer, not recursively:
    * The `next()` that hit the failure finishes (including its
      `movedToRow`) before the skip starts. Called inside it, the skip's
      `movedToRow` would come first, and the selection would end on the
      unplayable song.
    * Up to the shown playlist's song count of failures come in a row (a
      playlist on an unplugged drive): recursively, that many nested
      `next()` calls could overflow the stack; from the timer, the depth is
      constant.
    * Each attempt is one event-loop turn, so the window keeps repainting
      during a long run of failures, and the user can press Stop, which
      cancels a pending skip.
    * For `InvalidMedia`, which the player reports from its own
      `mediaStatusChanged`, only `player()->stop()` runs inside that emit;
      the next song starts afterwards
      ([event-ordering.md](event-ordering.md), "Re-entry in the app").
    * Cost: each skip starts one event-loop turn later, which is inaudible.
* After as many consecutive failures as the shown playlist has songs,
  playback stops and `playbackFailed(path)` is emitted; `MainWindow` shows
  `Cannot play file: <path>` in the status bar. The count resets when the
  position advances, or on stop.

## Seeking (`GstAudioPlayer`)

* `setPosition` runs a flushing, accurate seek
  (`GST_SEEK_FLAG_FLUSH | GST_SEEK_FLAG_ACCURATE`) with
  `gst_element_seek_simple`, on the main thread; so does the seek back to
  the old position after an output device change (`handleAudioOutputsChanged`).
* In a FLAC file, a seek plays from the exact sample, accurate or not: the
  parser (`GstBaseParse`, as `flacparse`) ignores `GST_SEEK_FLAG_KEY_UNIT` and
  starts the segment at the target, drops the frames that end before it
  undecoded, and the decoder trims the frame that holds it. The flags only
  choose where the parser starts reading.
* An accurate seek starts at the last known frame before the target (an
  entry in the parser's index: the file's seek table, or a frame already
  parsed), else at byte 0, and reads forward to the target on the
  streaming thread. The call returns at once.
* Rejected: a seek without `ACCURATE` (`GST_SEEK_FLAG_KEY_UNIT`). With no
  index entry near the target, `gst_base_parse_handle_seek` bisects from a
  bitrate estimate (`gst_base_parse_locate_time`, to up to 20 s before the
  target, `TARGET_DIFFERENCE`) on the thread that called seek, the UI
  thread, holding the parser's stream lock. In a FLAC file without a seek
  table it can land on a false sync code (`0xFFF8` in the audio data):
  `gst_flac_parse_frame_is_valid` returns 0 for the invalid header, which
  `gst_flac_parse_handle_frame` takes for a frame of size 0 (`if (next ==
  0) ret = TRUE`, GStreamer 1.26.5 and 1.28.7). With GLib's checks compiled
  out (vcpkg's Release build, `-Dglib_checks=false -Dglib_assert=false`, the
  app on Windows), `gst_base_parse_finish_frame` takes that empty frame: the
  seek freezes the app for seconds (up to about a minute), the parser posts
  `Failed to parse stream` (`gst_base_parse_check_sync`, no frame within 2
  MiB) and the song is skipped, or the parser thread crashes. With them
  compiled in (Homebrew, vcpkg's Debug build), it is rejected (`assertion
  'size > 0 || frame->out_buffer' failed`, logged) and playback resumes a
  fraction of a second late, but a few targets post `Internal data stream
  error` and skip the song.
* Cost, a known issue: in a FLAC file without a seek table, a seek ahead
  of the frames already parsed checks every byte from there for a frame
  start before playback resumes: near half a second near the end of a
  4-minute 96 kHz 24-bit file (3.2 Mbit/s). libFLAC's seek (foobar2000,
  DeaDBeeF) estimates the byte position, corrects it from frame headers and
  decodes part of one frame. A seek table in the file removes the cost.
  Fixes, not built, each also seeking without `ACCURATE` again:
  * Patch `flacparse` to skip one byte (`*skipsize = 1`) after an invalid
    header instead of finishing an empty frame, which causes every freeze,
    skip and crash, so the bisection resumes about as fast as libFLAC, at
    the same sample. Cost: a patched GStreamer (an overlay port on Windows;
    macOS and Linux keep the bug until a release fixes it), and the
    bisection's few small reads stay on the UI thread.
  * Build vcpkg's GStreamer with GLib's checks, as Homebrew does: build
    options in an overlay port, no patch. Cost: the logged warnings, late
    resumes and a few skipped songs remain, and every check runs in
    Release.
* Known issue (Windows, `wasapi2sink`, GStreamer 1.28.7): a seek can skip
  the song with `Failed to write to device` (`AUDCLNT_E_BUFFER_TOO_LARGE`),
  after many clicks around the seek slider. A flushing seek empties the
  sink's ring buffer; when it runs out partway through a write,
  `gst_wasapi2_rbuf_process_write` fills the rest with silence but asks
  `GetBuffer` for the last segment's `can_write` frames, not the frames
  left (`can_write_bytes` over the frame size), and WASAPI refuses when that
  is more. The app takes the error for
  an unplayable file ("Stopping and skipping"). Fix: GStreamer's, the
  frames left in that `GetBuffer` and `ReleaseBuffer`.

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

`PlaybackController` does not count plays: the Play Stats plugin does, from
the listener events `PluginPlayerHost` forwards (start, pause, resume,
position, duration, seek, end); see
[plugins/playstats.md](plugins/playstats.md), "Counting plays". `playSong`
repaints the started song's rows after its tag refresh
(`notifySongChanged`), so tags changed on disk and read at play show at
once, with the plugin disabled too.

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

## GStreamer bus messages (`GstAudioPlayer`, `PlaybackBackendManager`)

GStreamer's elements post bus messages (end-of-stream, errors, `ASYNC_DONE`)
from the thread they run on: a streaming thread, or the thread changing the
state. `GstAudioPlayer` reads them through a bus watch
(`gst_bus_add_signal_watch`), a source on GLib's default main context, so
`onBusMessage` runs on whichever thread runs that context. On macOS and
Windows a message takes two hops to reach `PlaybackController`:

1. **Streaming thread to the GLib thread.** The bus queues the message and
   wakes the default context. `PlaybackBackendManager` runs that context
   with a `GMainLoop` on its own `std::thread`, started with the GStreamer
   backend, and the watch calls `onBusMessage` there.
2. **GLib thread to the main thread.** `onBusMessage` emits
   `mediaStatusChanged` or `durationChanged`. Their receiver,
   `PlaybackController`, lives on the main thread, so Qt's `AutoConnection`
   queues the call with its arguments copied, and the main thread's event
   loop runs it.

On Linux there is one hop, onto the main thread: Qt's event dispatcher there
is GLib's (`QEventDispatcherGlib`), which runs the default context, so
`onBusMessage` runs on the main thread and its signals are direct calls.

A library that needs a GLib loop can be served in three ways: one loop runs
the other's sources, each loop gets its own thread, or the other loop is
skipped. Linux uses the first (Qt's event loop is GLib's), macOS and
Windows the second; the third is the planned alternative below.

* Why the thread on macOS and Windows: Qt's dispatchers there (Cocoa's run
  loop, the Win32 message queue) do not run GLib contexts, so without a
  thread running one no bus message would be handled.
* Rejected:
  * Running GLib's sources inside Qt's loop on macOS and Windows: driving
    the default context with `g_main_context_prepare`, `query`, `check`
    and `dispatch` around the system's wait (the reverse of Linux, where
    GLib's loop runs Qt's sources). Neither Qt nor GLib provides it for
    those loops, so the app would own the glue: GLib's handles in
    `MsgWaitForMultipleObjectsEx` on Windows (at most 64 handles), a
    `CFFileDescriptor` per fd and a `CFRunLoopTimer` for the deadline on
    macOS, updated whenever GLib's set of fds changes. The thread does the
    same job in a few lines.
  * Polling the bus (`gst_bus_pop`) from a `QTimer` on the main thread. A
    message waits up to the timer's interval, and the timer wakes the main
    thread even when nothing was posted.
* Planned, not built: a sync handler that delivers in one hop, skipping
  GLib's loop on every platform (below), part of
  [event-ordering.md](event-ordering.md), "Plan: A synchronous core with
  queued edges".
* Costs:
  * Delivery differs by platform: queued on macOS and Windows, direct and
    nested inside the bus dispatch on Linux
    ([event-ordering.md](event-ordering.md), "Delivery").
  * On macOS and Windows the whole `onBusMessage` switch runs on the GLib
    thread, not only the emits, so it may only do what is safe off the main
    thread: GStreamer calls such as `ASYNC_DONE`'s duration query on
    `playbin_`, which stays valid because `PlaybackBackendManager` joins the
    GLib thread before the player is deleted.
  * Linux needs a Qt built with GLib, as the distributions' Qt is: with
    `QT_NO_GLIB=1` or a Qt without GLib, its dispatcher is
    `QEventDispatcherUNIX`, nothing runs the default context, and no bus
    message is handled.

### Alternative: one hop through a sync handler (planned)

A sync handler (`gst_bus_set_sync_handler`) in place of the bus watch.
GStreamer calls it on the posting thread as the message is posted; it only
posts the message to the main thread and returns `GST_BUS_DROP`:

```cpp
GstBusSyncReply GstAudioPlayer::onSyncMessage(GstBus *, GstMessage *msg,
                                              gpointer data) {
  auto *self = static_cast<GstAudioPlayer *>(data);
  // GST_BUS_DROP hands msg's reference to the handler; the lambda owns it
  // and releases it when destroyed, also when Qt drops the call because
  // self is gone.
  std::shared_ptr<GstMessage> held(msg, gst_message_unref);
  QMetaObject::invokeMethod(
      self, [self, held] { self->handleBusMessage(held.get()); },
      Qt::QueuedConnection);
  return GST_BUS_DROP;
}
```

* `handleBusMessage` is today's `onBusMessage` switch. It runs on the main
  thread, in its own event-loop turn, on every platform, so its emits are
  direct calls that nest inside nothing.
* Only `GST_BUS_DROP` fits: `GST_BUS_PASS` would also queue the message on
  the bus, where nothing reads it, and `GST_BUS_ASYNC` would block the
  posting thread until someone pops it, which nobody does.
* The handler also runs on the main thread, inside the player's own
  `gst_element_set_state` (state-change messages); it posts there too, so
  that delivery does not nest either.
* Teardown sets the pipeline to `NULL` first, which stops the streaming
  threads, then clears the handler, so no streaming thread can call it once
  the player is gone. The state change itself posts messages on the main
  thread, inside the player's destructor; the handler queues them to the
  player, `QObject`'s destructor removes them unrun, and each lambda
  releases its message.

Gains:

* One delivery model: queued on every platform, and no nesting on Linux.
* No GLib thread: `PlaybackBackendManager::startGlibLoop` and
  `stopGlibLoop` go.
* Works on Linux with a Qt without GLib.
* The whole switch runs on the main thread, so it can use any of the
  player's state.

Costs:

* On Linux each message is delivered one event-loop turn later than today,
  which is inaudible.
* The handler runs on GStreamer's threads: it must only post, never change
  the pipeline (stopping it from a streaming thread deadlocks).
* Every message wakes the main thread, also those the switch ignores (each
  element's state changes, stream status), which the GLib thread absorbs
  today on macOS and Windows. A check of `GST_MESSAGE_TYPE` in the handler,
  safe on any thread, keeps them off it.

Unchanged: a message queued before `setSource` is still delivered after it,
as today on macOS and Windows, and the stale end-of-stream race
([event-ordering.md](event-ordering.md), "Known races") still needs its own
fix.

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
* Source: not GStreamer's documentation (`running.md` doesn't list
  `GST_PLUGIN_SCANNER`), but the GStreamer 1.28.7 source vcpkg builds (its
  archive is in vcpkg's `downloads` folder):
  `gstpluginloader-win32.c` (`find_helper_bin_location`,
  `gst_plugin_loader_new`, `gst_plugin_loader_retry_pending`),
  `gstregistry.c` (`gst_registry_scan_plugin_file`,
  `gst_registry_scan_path_level`), `gstinfo.h` (`GST_WARNING`), and vcpkg's
  `ports/gstreamer/portfile.cmake`. A newer GStreamer or vcpkg port
  may change it without notice.
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

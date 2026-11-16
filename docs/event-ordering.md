# Event ordering: re-entry and races

The order in which signal deliveries, commands and playback events run, and
what the app does when that order matters, and the plan for how commands
and events should flow. Two kinds of problems, in app code and plugins
alike:

* **Re-entry**: a receiver, run synchronously inside a delivery, calls back
  into the sender or changes what it reports on. The sender, its callers and
  the receivers after this one then resume with the state already changed:
  a second delivery can nest inside the first, and later receivers get an
  out-of-date value.
* **Races**: a command and an independent event (the end of a song, the
  user's click) both change the playback state, and whichever runs second
  wins, even when it was decided on state that has changed since.

A Qt signal with a direct connection (the default between objects on one
thread) is a method call, so both are the same for Qt code and for plugin
listener calls ([plugins/plugin-system.md](plugins/plugin-system.md),
"Events: subscription"). Which code runs on which thread is in
[architecture.md](architecture.md), "Threading / Async Model".

All races below are rare, except the slider drag across the end of a song,
which needs only a drag held past the end; each is listed with the exact
timing it needs.

## Delivery

* **Synchronous, on the main thread.** `PlaybackController`'s signals,
  `PluginPlayerHost`'s listener calls and the backends' signals emitted on
  the main thread (`GstAudioPlayer`'s position timer, its `stop()`) run
  their receivers before returning.
* **Queued, on macOS and Windows, for GStreamer's bus messages.**
  `GstAudioPlayer::onBusMessage` emits `mediaStatusChanged` and
  `durationChanged` on a GLib thread there, so they reach
  `PlaybackController` as queued calls. On Linux it runs on the main
  thread, so they are direct calls, nested inside the bus dispatch. How
  each platform gets there, and the planned one-hop delivery that queues on
  every platform ("Plan: A synchronous core with queued edges"), are in
  [playback.md](playback.md), "GStreamer bus messages".
* **Positions.** `DummyAudioPlayer` emits `positionChanged` inside
  `setPosition` and `setSource`; `GstAudioPlayer` reports positions only
  from its 100 ms timer, so a seek does not deliver a position inside
  itself.
* **No order between ready events.** Qt does not specify the order between
  a posted call, a pending timer and pending input when several are ready;
  it depends on the platform's event dispatcher (a thread's message queue
  on Windows, GLib sources on Linux, Cocoa's run loop on macOS). Input that
  arrives while the main thread runs a delivery waits in the OS's queue and
  is handled after it, before or after calls queued during the delivery.
  Nothing in the app relies on this order, and a fix for a race must not
  either.

## Rules in use

* **A listener does not add or remove listeners during a call**
  ([plugins/plugin-system.md](plugins/plugin-system.md),
  "Events: subscription").
* **A skip of an unplayable song is deferred** with a zero-delay timer
  (`skipFailedSongTimer_`), not called recursively: the app's own case of
  deferring a call out of the delivery that triggered it. The reasons are in
  [playback.md](playback.md), "Stopping and skipping".
* **State is updated before the signal that reports it.** When the playing
  song is removed from the library, `currentSongPk_` is reset before
  `stop()`, so `stop()`'s receivers see no current song.
* **Plugin commands are deferred**: `PluginPlayerHost` queues each one to
  the main thread's event loop, so a command called inside a listener call
  runs after every listener has the current value ("Plugin commands: the
  contract"; [plugins/plugin-system.md](plugins/plugin-system.md),
  "Commands: `IPlayerControl`").
* **The seek slider seeks only on release** (`sliderReleased`), and
  `positionChanged` moves it only while it is not held, so setting its value
  never seeks: no loop between the two.

## When a receiver may change the sender

A receiver that changes the emitting object (or what it reports on) runs
while the emit, the emitting method and that method's callers are all still
on the stack; each of them resumes afterwards, with the state already
changed. That is safe only if all four hold:

1. **The emitter emits last.** Its state is complete before the emit, and
   nothing after the emit relies on the old state. This protects only the
   emitting method itself.
2. **Its callers do nothing after the call that relies on the old state.**
   A method that emits last still returns to a caller, which continues:
   `finishStream(); stats_.record(source_);` would record the next song if
   `finishStream()`'s receiver had switched songs.
3. **Later receivers can take an out-of-date report**, or there is only one.
   Slots run one after another; a receiver connected after the one that
   switched songs still gets `EndOfMedia` for a song that is no longer
   playing.
4. **No receiver deletes the sender directly.** The code returning through
   the emitter and its callers would touch a destroyed object; a receiver
   uses `deleteLater()` instead, which deletes once control is back in the
   event loop.

None of the four is enforced: a comment at an emit that a receiver may
answer by changing the sender says which ones it relies on.

## Re-entry in the app

All of it is safe:

| Where | What nests | Why it is safe |
| --- | --- | --- |
| `onMediaStatusChanged(EndOfMedia)` | On Linux, `next()` → `setSource` and `play` run inside `GstAudioPlayer::onBusMessage`, on the player that is emitting; on macOS and Windows the signal is queued from the GLib thread, so nothing nests | GStreamer allows state changes from the application thread that handles bus messages (not from its streaming thread), and the four conditions hold: each emit in `onBusMessage` is the last statement of its case (the error case frees the parsed error before it); the caller is GLib's bus dispatch, which only releases the message; `mediaStatusChanged` has one receiver, `PlaybackController`; nothing deletes the player inside the call. `QtAudioPlayer` forwards `QMediaPlayer::mediaStatusChanged`, from which changing the source is the usual way to play the next file |
| `onMediaStatusChanged(InvalidMedia)` | `handlePlaybackFailure` → `player()->stop()` → `GstAudioPlayer::stop` emits `positionChanged(0)` → `onPositionChanged`; the skip to the next song is deferred, so only the stop nests | `onPositionChanged` resets the failure count only for a position above 0; the other conditions hold as for `EndOfMedia` |
| `PlaylistCollection::playlistAboutToBeRemoved`, `songsRemovedFromLibrary` | `stop()` and its `playbackStateChanged` receivers | The state is updated first ("Rules in use") |

## Known races

### A stale end-of-stream undoes a seek

* **Timing**: the user releases the seek slider within the few milliseconds
  between GStreamer posting end-of-stream and the main thread handling it,
  so at the very end of a song:

  | Thread | Event |
  | --- | --- |
  | Streaming | The sink plays the last sample and posts end-of-stream on the bus |
  | Main | The mouse release is handled first: `seek(ms)` runs a flushing seek |
  | Main | The end-of-stream arrives (from the bus watch on Linux, as a call queued by the GLib thread on macOS and Windows): `EndOfMedia` → `next()` |

  The next song starts, and the seek is lost.
* **Cause**: the end of a song is concluded on one thread (the sink) and
  acted on by another (`PlaybackController`) later, without a check. A
  flushing seek clears the audio data in the pipeline, not messages already
  on the bus (flushed only when the pipeline stops) or calls already queued
  to the main thread.
* **Fix, not built**: `GstAudioPlayer` records the sequence number of its
  last flushing seek and ignores an end-of-stream from before it. This
  relies on GStreamer passing a seek's sequence number on to the
  end-of-stream that follows it; check that before building it. Not built:
  the user was at the very end of the song, and the next song playing is a
  mild outcome.
* **Structural alternative**: gapless playback (`playbin`'s
  `about-to-finish`), where the next song is queued inside the pipeline and
  the switch is reported on the bus afterwards (`STREAM_START`), so no
  message carries a decision. A much larger change (see "How other players
  handle it"); worth it for gapless playback itself, not for this race.

### A slider drag across the end of a song seeks the next song

* **Timing**: the user holds the seek slider while the song reaches its
  end. No timing luck is needed, only a drag that lasts past the end:
  1. While the slider is held, `positionChanged` does not move it, and
     playback continues.
  2. The song ends: `EndOfMedia` → `next()`; `durationChanged` sets the
     slider's maximum to the next song's length (which clamps the value if
     the next song is shorter).
  3. On release, `seek(slider value)` is applied to the next song.
* **Cause**: the seek was meant for a playback session that ended during the
  drag, and nothing tells the two apart.
* **Fix, not built**: the playback generation ("Plan: A synchronous core
  with queued edges"), noted on `sliderPressed` and checked on release.

### A plugin seek lands in the next song

* **Timing**: a plugin's `seek` is queued and a song change runs before
  it. With the A-B Repeat plugin, B in the last position update of a song
  (100 ms with GStreamer):

  | Thread | Event |
  | --- | --- |
  | Main | The position timer reports B: the plugin queues `seek(A)` |
  | Main | The end-of-stream, pending in the same turn: `EndOfMedia` → `next()` |
  | Main | The queued seek runs on the next song, which starts at A |

  A click on a Lyrics line just before the song ends is the same: its seek
  runs on the next song. As the last song of the playlist ends, its `play`
  runs after the stop and starts the shown playlist.

* **Cause**: the seek was meant for the session it was called in, and
  `seek` is not bound to one.
* **Fix, not built**: the generation check on plugin seeks ("Plugin
  commands: the contract", rule 6).

## Plan: A synchronous core with queued edges

Status: partly implemented. Built: plugin commands, `seek` and `play`, not
yet checked against the generation
([plugins/plugin-system.md](plugins/plugin-system.md), "Commands:
`IPlayerControl`"). Planned: the playback generation, with the slider fix
("A slider drag across the end of a song seeks the next song") and the
check on plugin seeks ("A plugin seek lands in the next song"); then
one-hop delivery of GStreamer's bus messages.

Playback state lives on the main thread and changes synchronously; whatever
comes from outside reaches it as one queued call:

* **One command API, `PlaybackController`.** The UI, the system media
  controls and plugins call the same methods; plugins through
  `IPlayerControl`, which only queues and checks the generation.
* **A synchronous core.** Signals are direct calls, and commands take effect
  before they return. A slot that changes the sender meets the four
  conditions of "When a receiver may change the sender", or defers that one
  call, with a comment saying why.
* **Each edge queues once** ("Who queues each edge"). Work from another
  thread, a plugin or a library's dispatch reaches the core as one queued
  call on the main thread, never nested inside a library's dispatch.
  GStreamer's bus does not yet (two hops on macOS and Windows, nested on
  Linux); a sync handler ([playback.md](playback.md), "Alternative: one hop
  through a sync handler") makes it one queued hop everywhere, without the
  GLib thread.
* **Synchronous events to plugins**, as today
  ([plugins/plugin-system.md](plugins/plugin-system.md),
  "Events: subscription"): a listener sees the current value and may update
  its UI directly. It returns quickly; longer work runs on the plugin's own
  thread, and its result comes back as a command or the plugin's own queued
  call.
* **One playback generation** ("The playback generation") for whatever can
  go stale: the slider's release, plugin seeks, and a plugin's own
  asynchronous results, such as a lyrics fetch, which `trackStarted` lets it
  check by carrying the generation.

### The playback generation

A counter in `PlaybackController`, incremented whenever a playback session
ends: a song starts (also the same song again) or playback stops. A command
meant for the session it was issued in captures the generation when it is
issued and is dropped if the generation has changed when it runs. The
song's id would not do: the same song can start again (repeat one, a song
twice in the playlist), and a stale command would pass. It closes races
between a command and a song change; it does not help with a stale
end-of-stream, which is not a command.

### Who queues each edge

An edge is where work reaches the core from something it does not control:

* **Another thread**: efsw's watcher, the artwork threads, SMTC callbacks,
  the cloud sync worker, the GLib thread on macOS and Windows.
* **Code that cannot be reviewed**: a plugin's command, even from the main
  thread inside a listener call.
* **A library's own call stack**: on Linux bus messages arrive on the main
  thread, but nested inside GLib's dispatch of the bus watch.

Not edges: UI input, which Qt delivers at the top of the main thread's
stack, outside any delivery; the app's own slots calling each other, which
are inside the core ("When a receiver may change the sender"); and calls out
of the core, such as listener calls to plugins, which stay synchronous.

Each edge queues in one place: the code that owns it, at the boundary. The
core (`PlaybackController` and what it calls) never queues and never runs on
another thread.

| Edge | Queued by | How |
| --- | --- | --- |
| GStreamer's bus → `PlaybackController` | `GstAudioPlayer` | The `AutoConnection` from the GLib thread on macOS and Windows, a nested direct call on Linux; planned: the sync handler's `invokeMethod` on every platform |
| efsw's watcher thread → `MonitoredLibrary` | `EfswFileSystemMonitor::Listener` | `invokeMethod` to the monitor |
| Artwork threads → the system media interfaces | Each interface | `invokeMethod` to itself, with a task id |
| SMTC callbacks (Windows) → `WindowsMediaCenter` | `WindowsMediaCenter` | `invokeMethod` to itself |
| Cloud sync worker ↔ `CloudPlayStatsSyncCoordinator` | The coordinator | `invokeMethod` both ways |
| Plugin commands → `PlaybackController` | The host (`IPlayerControl`) | The plugin calls a plain method, from anywhere; the host queues it; planned: it checks the generation |

* **App edges are queued by hand** in the class that owns the foreign
  thread. The rule (code on another thread queues before it touches the
  core) is checked in review; only signals have it built in, through
  `AutoConnection`.
* **The plugin edge is queued by the host**, so it holds for code that cannot
  be reviewed: a plugin cannot skip it and need not know about it.
* **A plugin's own edges are the plugin's.** A result from its worker thread
  for its own widget (a lyrics fetch) is queued by the plugin; the host
  covers only calls into the host.

### Plugin commands: the contract

`IPlayerControl`, reached through `IPluginHost::control()`; `seek` and
`play` are built, `pause` comes with a plugin that uses it. Its header
states all but rule 6, which comes with the generation:

1. **Deferred.** A command returns at once; the host queues it
   (`QMetaObject::invokeMethod` with `Qt::QueuedConnection`, the host object
   as the context) and runs it in a later turn of the main thread's loop.
   Code right after the call sees the old state: `position()` right after
   `seek(a)` gives the old position.
2. **In order.** Commands run in the order they reached the queue.
3. **Allowed inside a listener call.** The command runs after every listener
   has the current value.
4. **Callable from any thread.** Queuing makes it safe; the generation is
   read atomically at the call.
5. **Reported by events.** A command's effect arrives through the listener
   events once it has run, never inside the call. There is no return value,
   and a command dropped or without effect (`pause` with nothing playing)
   reports nothing.
6. **Bound to a session, or not.** `seek` captures the generation when
   called, which is the session the plugin was looking at (it calls from a
   synchronous delivery, its own UI or its own thread), and is dropped if
   the generation has changed when it runs. `play` and `pause` apply to
   whatever plays then. The header says which each command is.
7. **Not merged.** Every queued command runs.
8. **Dropped at shutdown.** Commands still queued when the host is destroyed
   are dropped. Plugins are loaded at startup and unloaded at exit only
   (disabling takes a restart,
   [plugins/plugin-system.md](plugins/plugin-system.md)), so no
   plugin goes away while its commands are queued.

* **The timing the generation check closes** is in "A plugin seek lands in
  the next song": with the check, the queued seek is dropped, and the next
  song plays from its start.
* **Deferral fixes re-entry, not races.** It adds one event-loop turn to a
  race's window; only the check when the command runs closes it.

### Rejected

* **Synchronous commands.** A seek inside `positionChanged` is a receiver
  changing the sender; it meets conditions 1, 2 and 4 of "When a receiver may
  change the sender", not condition 3. With a backend that emits the
  position inside `setPosition` (the test player), it nests a second
  delivery, and the receivers after the plugin (other listeners, possibly
  the slider and the spectrum, by connection order) get the old position
  after the new one: the lyrics panel shows 30 s while playback is at 10 s,
  until the next position update. `GstAudioPlayer::setPosition` emits no
  position, so with GStreamer nothing nests and a correct plugin's seek is
  harmless. The weightier reason is a buggy plugin: one that seeks again on
  the position its seek produces (to a point at or past B, say) recurses
  without bound with a backend that emits synchronously, and crashes; with
  deferral it is an endless chain of queued seeks, one per event-loop turn,
  and the app stays responsive. Deferring makes both independent of the
  backend.
* **Deferring every command, the app's own too**, as DeaDBeeF does by
  queuing every command, its UI's included. App code relies on commands
  taking effect at once: `PlaybackController::setBackend` stops playback
  before the old player is destroyed, removing the playing song resets
  `currentSongPk_` before `stop()`, the selection follows the `movedToRow`
  that `next()` emits before returning, and the tests check a command's
  result on the next line. Each of these would wait for the signal that
  reports the result instead, and every command bound to a song would need
  the generation check, since each would have a window in which the song
  changes (DeaDBeeF's seek landing in the next track). The gain, no
  re-entry, matters only where the code cannot be reviewed: the app's own
  re-entry is the three cases in "Re-entry in the app", each checked
  against "When a receiver may change the sender". DeaDBeeF can afford it
  because it is message-driven throughout: its UI reacts to the events
  that follow a command, and its commands cross threads anyway.
* **One queue for events and commands**, as DeaDBeeF. Listeners would get
  each event a turn late, after the state may have moved on, and on a
  message thread they would hand every UI update to the main thread; the
  synchronous delivery already gives each listener the current value.
* **Deferring only commands called inside a delivery.** A "delivering" flag
  shared by `PluginPlayerHost` and the control, and commands that behave
  differently depending on where they are called, to save one event-loop
  turn nobody hears.
* **A token passed by the plugin** (`seek(token, ms)`). The intent is
  explicit, but plugins have to keep the token; capturing at call time gives
  the same result.
* **A documented rule instead** ("do not call commands from inside a
  listener call", as foobar2000). The safe behavior would depend on every
  plugin author remembering it.

### Costs

* A command takes effect one event-loop turn later; code right after it
  sees the old state, which the header states.
* Two command models: synchronous for app code, deferred for plugins; the
  header says which a plugin gets.
* The generation counter and a check per session-bound command.
* The sync handler's costs ([playback.md](playback.md), "Alternative: one
  hop through a sync handler").
* Tests that let the event loop run (`QTRY_COMPARE`): a listener seeking
  inside `positionChanged` (the test player emits synchronously, so nesting
  would show; built), and, with the generation, a song change, also to the
  same song, before the queued seek runs.

## How other players handle it

### foobar2000

From the SDK headers only (the core is closed source):

* Playback commands (`playback_control`) and playback callbacks
  (`play_callback`) are main-thread only, and the documentation forbids
  calling commands from inside a callback: "Do not call `playback_control`
  methods from inside any kind of global callback (e.g. playlist callback),
  otherwise race conditions may occur" (`playback_control.h`). A component
  defers its call with `fb2k::inMainThread`, "to execute some code outside
  the current call stack / global fb2k callbacks", in a guaranteed FIFO
  order (`threadsLite.h`, `main_thread_callback.h`). The core most likely
  runs commands at once (otherwise the rule would not be needed): deferral
  is the component's job.
* `playback_seek(double)` "Seeks in currently played track", with no track
  argument; the SDK does not say what happens to a deferred seek after the
  track changed.
* Decoding and output run on a playback thread that the core owns. The
  output is flushed after a seek, the core knows the exact latency between
  queued and heard audio (`output.h`), and the output's obsolete track marks
  suggest that track boundaries were once markers in the audio stream. So a
  stale end of track most likely cannot undo a seek, as in DeaDBeeF.

### DeaDBeeF

From its source (`src/messagepump.c`, `src/main.c`, `src/streamer.c`):

* **One queue for events and commands.** Plugins send with `sendmessage`
  and receive with `message`; commands (`DB_EV_SEEK`, `DB_EV_NEXT`, ...)
  are messages in the same id space as events. `sendmessage` only appends to
  a mutex-protected FIFO (`messagepump_push`). A message thread
  (`player_mainloop`) pops each message, broadcasts it to every plugin, then
  runs it if it is a command. A command sent inside `message()` therefore
  runs after the whole broadcast: deferral is built into the host, and
  plugins cannot re-enter. The queue holds 100 messages; a message sent to a
  full queue is dropped silently.
* **Plugin callbacks run on the message thread**, not the GUI thread; the
  GTK UI plugin passes work to GTK with `g_idle_add`.
* **A seek crosses two queues**: the message thread calls
  `streamer_set_seek`, which queues `STR_EV_SEEK` for the streamer thread,
  which runs `streamer_seek_real` between decoded blocks, then sends
  `DB_EV_SEEKED` as a report.
* **The end of a track is a marker in the decoded data.** Playback is
  gapless: the streamer decodes the next track ahead and flags its first
  block. The output thread switches `playing_track` when it reads that block
  (`handle_track_change` in `_streamer_get_bytes`), under `streamer_lock`. A
  seek seeks `playing_track` (what is heard), pulls the decoder back from
  the next track if it had moved on, and clears the decoded blocks under the
  same lock (`streamer_reset`). Nothing left over can undo a seek.
* **A seek can land in the next track.** `DB_EV_SEEK` carries no track; a
  track change resets only the displayed position (`last_seekpos`), not the
  queued `STR_EV_SEEK`, and only a stop clears the streamer's queue
  (`handler_reset`). If the output thread reaches the next track's first
  block before the seek runs, the next track is seeked. `playing_track`
  switches when the block is read, ahead of the device's buffer, which can
  widen this window.

### Compared

| | This app | foobar2000 (SDK) | DeaDBeeF (source) |
| --- | --- | --- | --- |
| Commands from inside a callback | Allowed, deferred by the host | Forbidden by the documentation; the component defers | Allowed; every message is queued |
| Where deferral happens | The host, for plugin commands | The component (`inMainThread`) | The host, for everything |
| Plugin callbacks' thread | Main (UI) thread | Main (UI) thread | A message thread |
| A stale end of a song or track undoes a seek | Possible (end-of-stream on the bus) | Most likely not | Not possible (a marker under a lock) |
| A seek meant for one song or track lands in the next | Possible (slider drag, plugin seeks; planned: guarded by the generation) | Not addressed in the SDK | Possible, unguarded |

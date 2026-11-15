# Events and Threads

A guide to how work reaches the code that handles it in this app: Qt's event
loop, events, thread affinity, signals and slots, GStreamer's threads and
bus, and the app's threads. Read it before changing a signal handler,
anything that runs off the main thread, or the playback backend. The design
choices, with their reasons and costs, are in the docs it links to:
[docs/architecture.md](../docs/architecture.md) ("Threading / Async
Model"), [docs/playback.md](../docs/playback.md) ("GStreamer bus messages")
and [docs/event-ordering.md](../docs/event-ordering.md).

## The event loop

A GUI program spends most of its time waiting. Its main thread runs a loop:

```
loop:
  wait until something is ready: input, a timer, a posted event, a file
  handle each thing that is ready
```

In Qt that loop is `QCoreApplication::exec()`, and the code that waits and
dispatches is the thread's **event dispatcher**. It sits on the platform's
own mechanism:

| Platform | What the main thread waits on | Qt's dispatcher |
| --- | --- | --- |
| Windows | The thread's Win32 message queue (`MsgWaitForMultipleObjectsEx`) | `QEventDispatcherWin32` |
| macOS | Cocoa's run loop (`CFRunLoop`) | `QCocoaEventDispatcher` |
| Linux | GLib's main context (`GMainContext`, over `poll`), when Qt is built with GLib | `QEventDispatcherGlib` (in a GUI app, its subclass for the display server) |

Windows and macOS provide a loop as part of the system. Linux provides only
the parts (`poll`, `epoll`, `eventfd`, the display server's socket); each
toolkit brings its loop, and GLib's is the one most of the Linux desktop
shares: GTK, GIO, D-Bus bindings and GStreamer use it, and Qt runs on it so
their sources are dispatched too ("Two loops on one thread", under
GStreamer). A Qt built without GLib, or run with `QT_NO_GLIB=1`, uses
`QEventDispatcherUNIX`, a plain `poll` loop.

Three consequences run through the rest of this guide:

* **One thing at a time.** While a handler runs, the loop does not; nothing
  else on that thread runs until the handler returns. A click that arrives
  meanwhile waits in the OS's queue. The exception is a **nested loop**: a
  modal `exec()` (`QMessageBox::warning`, a dialog's `exec()` in
  `SongPropertiesDialog`) or a `QEventLoop` (the cloud sync service waits
  in one for each HTTP reply and retry delay, on the sync thread) runs the
  loop inside the handler that started it, so queued calls, timers and
  input run before that handler returns.
* **Each thread has its own loop, or none.** A `QThread` runs one if its
  `run()` calls `exec()` (the default). A `std::thread` or a library's thread
  has no Qt loop.
* **The order between kinds of ready work is not specified.** When a posted
  event, a timer and input are all ready, which runs first depends on the
  dispatcher.

## Events

An **event** is an object (a `QEvent` subclass) delivered to one `QObject`:
a mouse click, a paint request, a timer tick, a deferred delete. The
receiver handles it in `event()`, or in a typed handler such as
`mousePressEvent` or `customEvent`. Event filters
(`installEventFilter`) can see or block an event before the receiver does.

There are two ways to deliver one:

* **`QCoreApplication::sendEvent(receiver, &event)`** calls the receiver now,
  on the calling thread. It is a function call; the caller keeps the event.
* **`QCoreApplication::postEvent(receiver, new Event)`** queues the event and
  returns at once. Qt takes ownership and deletes it after delivery. It is
  safe from any thread:
  1. it appends the event to the posted-event list of the thread the
     receiver lives in, under that list's mutex;
  2. it wakes that thread's dispatcher (`PostMessage` on Windows,
     `CFRunLoopWakeUp` on macOS, `g_main_context_wakeup` on Linux);
  3. that thread's loop later calls `sendPostedEvents()`, which delivers the
     posted events in the order they were posted (within one priority; the
     default is normal).

  A receiver destroyed before delivery has its pending events removed.

The app never calls `postEvent` itself; it uses it through queued signals,
`QMetaObject::invokeMethod` and `deleteLater`, below.

## Thread affinity

Every `QObject` **lives in** one thread. That is its **thread affinity**
(`QObject::thread()`). It is a delivery address: events posted to the
object, queued calls to it, its timers and its `deleteLater` run on that
thread's loop.

How an object gets its thread:

* **At construction**, the thread that creates it. A child created with a
  parent takes the parent's thread (a parent and its children always share
  one), so `new QTimer(this)` in `GstAudioPlayer` lives where the player
  does.
* **`moveToThread(target)`** changes it, for the object and its children.
  It is called from the object's current thread, on an object with no
  parent.
* **Widgets** must live in the main thread.

What it does not do:

* **It does not redirect direct calls.** Calling a method of an object
  from another thread runs the method on the calling thread, whatever the
  object's affinity.
* **It is not a lock.** State touched from two threads still needs one.
* **It needs a running loop.** An object living in a thread with no Qt loop
  never gets its queued calls, timers or `deleteLater`.

All of the app's objects are created on the main thread and live there,
except the cloud sync worker. "Affinity at work", after signals, shows
where that matters.

## Signals and slots

A **signal** is a member function that moc, Qt's code generator, writes for
you; `emit` is an empty macro, so `emit mediaStatusChanged(s)` is a call to
it. The generated function calls `QMetaObject::activate`, which goes through
the signal's connections, in the order they were made, and for each one
decides how to call the slot:

| Connection type | What happens at the emit |
| --- | --- |
| `Qt::DirectConnection` | The slot is called now, on the emitting thread: a plain function call. |
| `Qt::QueuedConnection` | Qt copies the arguments into a `QMetaCallEvent` and posts it to the receiver; the slot runs later, on the thread the receiver lives in. |
| `Qt::AutoConnection` (the default) | Decided at each emit: direct if the emitting thread is the thread the receiver lives in, queued if not. The sender's own affinity plays no part. |
| `Qt::BlockingQueuedConnection` | Queued, and the emitting thread waits until the slot has run. Deadlocks if both are the same thread. |

So a signal is not asynchronous by itself. Between objects on one thread
the default is a direct call: every receiver has run by the time `emit`
returns, and a receiver that calls back into the sender does so while the
sender is still inside the emit (re-entry, in
[docs/event-ordering.md](../docs/event-ordering.md)). Only a queued
connection defers the slot.

A queued call copies its arguments, so their types must be known to Qt's
metatype system: Qt's own types, `Q_ENUM`s such as
`QMediaPlayer::MediaStatus`, or types declared with `Q_DECLARE_METATYPE`.

`QMetaObject::invokeMethod(context, lambda, Qt::QueuedConnection)` is the
same queued call without a signal: it posts a `QMetaCallEvent` holding the
lambda to `context`, the **context object**, and the lambda runs on the
thread `context` lives in. It is how a thread with no Qt loop hands work to
one that has one. Without `Qt::QueuedConnection` (the default is
`AutoConnection`), a call from `context`'s own thread would run the lambda
at once.

Signals and events compared:

| | `emit signal(args)` | `postEvent(receiver, event)` |
| --- | --- | --- |
| Who receives | Whatever is connected; the sender does not know | The one receiver named |
| When | Direct: inside `emit`. Queued: later, on the thread the receiver lives in | Always later, on the thread the receiver lives in |
| Use | "This happened", to whoever listens | "Do this", to a known object |

## Affinity at work

### A signal across threads: bus messages on macOS and Windows

`PlaybackController` connects to the player with the default connection
type (`playbackcontroller.cpp`, `connectPlayer`):

```cpp
connect(player(), &AudioPlayer::mediaStatusChanged,
        this, &PlaybackController::onMediaStatusChanged);  // AutoConnection
```

On macOS and Windows, `GstAudioPlayer::onBusMessage` runs on the GLib
thread ("Running GLib's loop") and emits there:

1. `emit self->mediaStatusChanged(EndOfMedia)` calls the generated signal
   function, which calls `QMetaObject::activate`.
2. For the connection, `activate` compares the **emitting thread**, the
   GLib `std::thread` (Qt gives a thread it did not start an "adopted"
   thread record on first use), with the **thread the receiver lives in**,
   `PlaybackController::thread()`: the main thread, where it was created.
3. They differ, so the `AutoConnection` acts as a `QueuedConnection`: Qt
   copies `EndOfMedia` into a `QMetaCallEvent` and posts it to
   `PlaybackController`.
4. The emit returns at once, and `onBusMessage` finishes on the GLib
   thread.
5. The main loop delivers the event: `onMediaStatusChanged(EndOfMedia)`
   runs on the main thread.

The sender, `GstAudioPlayer`, lives on the main thread too, but only the
emitting thread and the receiver's thread are compared. On Linux both are
the main thread, so the same `connect` gives a direct call: that one
comparison is the whole platform difference.

### A queued call from a foreign thread: `invokeMethod`

efsw reports file changes on its own watcher thread
(`efswfilesystemmonitor.cpp`, `Listener::handleFileAction`):

```cpp
QMetaObject::invokeMethod(
    monitor_,                                    // the context object
    [monitor = monitor_, paths]() { emit monitor->pathsChanged(paths); },
    Qt::QueuedConnection);
```

1. The context object chooses the thread. `monitor_`
   (`EfswFileSystemMonitor`) lives on the main thread, so the lambda is
   posted to the main thread's queue.
2. The calling thread does not matter: the efsw thread only posts and
   returns; the lambda never runs there.
3. The main loop runs the lambda. Its `emit monitor->pathsChanged(paths)`
   now happens on the main thread, so `MonitoredLibrary`, also on the main
   thread, receives it as a direct call.

Three things follow:

* **Capture by value.** The lambda runs later, on another thread, so it
  copies `paths`; a reference into the efsw thread's stack would dangle.
* **The context object bounds the call.** If `monitor_` is destroyed
  before the event is delivered, Qt removes the event and the lambda never
  runs. Posting to an object already destroyed is still wrong, so
  `EfswFileSystemMonitor` stops the efsw thread before it goes.
* **`Qt::QueuedConnection` states the intent.** The caller is never the
  main thread, so `AutoConnection` would queue too; naming it makes the
  hand-off explicit.

The system media interfaces do the same with `self`: each prepares a cover
image on a `std::thread` of its own, then posts the result to `self`, which
lives on the main thread, with a task id so that an outdated result is
dropped.

The two compared:

| | A signal (`mediaStatusChanged`) | `invokeMethod(context, ...)` |
| --- | --- | --- |
| Who decides "queued" | Qt, at each emit, by comparing threads | The caller, with `Qt::QueuedConnection` |
| Whose affinity picks the thread | The receiver's (`PlaybackController`) | The context object's (`monitor_`, `self`) |
| What travels | The signal's arguments, copied | The lambda with what it captured, by value |
| Same code run on the main thread | Direct (as on Linux) | Still queued |

### A moved object: the cloud sync worker

`CloudPlayStatsSyncCoordinator` owns a worker and a `QThread`, and moves
the worker to that thread when it is created:

```cpp
syncWorker_.moveToThread(&syncThread_);
syncThread_.start();
```

From then on the worker lives on the sync thread. The coordinator posts
each HTTP task to it with `invokeMethod(&syncWorker_, ...)`, so the task
runs on the sync thread; when the task has a result, it posts it on with
`invokeMethod(coordinator, ...)`, which runs it on the main thread, where
the coordinator lives. Each hop picks its thread by its context object's
affinity. An object must be destroyed on the thread it lives in, so before
stopping the thread the coordinator's destructor runs
`syncWorker_.moveToThread(ownerThread)` on the sync thread itself, with a
`Qt::BlockingQueuedConnection` that waits until the move is done.

### Elsewhere

* **`GstAudioPlayer`'s position timer** is the player's child, so it lives
  on the main thread and fires there on every platform ("Positions").
* **`deleteLater` of the old player** runs on the main loop, after
  `PlaybackBackendManager` has joined the GLib thread ("Rules that
  follow").

## Deferring on the same thread

Two tools run code in a later turn of the same thread's loop, after the
current handler and everything below it on the stack have returned:

* **A zero-delay timer** (`QTimer::singleShot(0, ...)`, or a single-shot
  `QTimer` with interval 0). `PlaybackController` skips an unplayable song
  this way ([docs/playback.md](../docs/playback.md), "Stopping and
  skipping").
* **`invokeMethod(context, ..., Qt::QueuedConnection)`** with a context
  object that lives on the same thread.

Both mean "after the current handler", but Qt does not order a timer
against a posted call. `deleteLater` is the same idea for deletion: it posts
a deferred-delete event, so an object is not destroyed while a call on it
is still running higher up the stack.

## GStreamer

GStreamer plays audio as a **pipeline** of elements: a source reads the
file, a decoder decodes it, filters change it, a sink sends it to the audio
device. The app uses `playbin`, an element that builds that pipeline itself,
with the app's ReplayGain `volume` element as its audio filter
([docs/playback.md](../docs/playback.md), "ReplayGain").

### Streaming threads

Data flows through the pipeline on GStreamer's own **streaming threads**,
not on the app's threads. The app's code runs there in two ways:

* **Pad probes**: callbacks on an element's pad, called for each buffer or
  event that passes, on the streaming thread. `GstAudioPlayer` has three:
  the PCM tap for the spectrum (`onPcmProbe`), the bitrate counter
  (`onBitrateProbe`) and the ReplayGain tag reader (`onTagProbe`).
* **Signals of elements**, which GStreamer emits on whatever thread is
  acting, such as `playbin`'s `element-setup`.

These callbacks must be quick and must not stop the pipeline: going down to
`READY` or `NULL` stops the streaming threads and waits for them, so asking
for it from a streaming thread deadlocks.

The probes call into `GstAudioPlayer`, a `QObject` that lives on the main
thread, directly from the streaming thread. Affinity does not redirect a
direct call and is not a lock ("Thread affinity"), so the probes touch only
state guarded for two threads: the PCM tap behind a mutex (`pcmTap()`), the
ReplayGain tags behind `replayGainMutex_`, and the bitrate count, an
atomic. GStreamer objects themselves have no affinity: an element can be
called from any thread, and GStreamer locks its own state.

### The bus

Elements report what happens (end-of-stream, an error, a finished state
change such as `ASYNC_DONE`) by posting a **message** on the pipeline's
**bus**. The bus is a thread-safe queue: an element posts from the thread
it is running on (a streaming thread, or the thread changing the state),
and someone else takes the message off. There are three ways to take it:

* **A bus watch** (`gst_bus_add_signal_watch`, then the bus's `message`
  signal): a GLib source on a `GMainContext`. The thread that runs that
  context calls the handler. This is what `GstAudioPlayer` uses.
* **A sync handler** (`gst_bus_set_sync_handler`): called on the posting
  thread, at the moment of posting, before the message is queued. This is
  the planned alternative below, not built yet.
* **Popping** (`gst_bus_pop`, `gst_bus_timed_pop`): the caller asks for the
  next message. Popping from a `QTimer` on the main thread was rejected
  ([docs/playback.md](../docs/playback.md), "GStreamer bus messages").

### Two loops on one thread

A bus watch, like GIO and GLib's D-Bus bindings, attaches its work to a
GLib `GMainContext` and expects someone to run it. Qt has its own loop, and
a thread can block in only one wait call at a time, so the two loops cannot
both wait on one thread. There are three ways out.

**What GLib's loop is made of.** A `GMainContext` holds **sources**: things
that can become ready. One turn of the loop (an iteration) asks each source
four things:

| Phase | GLib asks each source | The bus watch answers |
| --- | --- | --- |
| `g_main_context_prepare` | "Ready already? If not, when will you be?" | Ready if a message is already queued |
| `g_main_context_query` | "Which fds should I wait on?" | The bus's wake-up fd |
| (wait) | GLib blocks in `poll()` on those fds, until the earliest deadline | A posting thread writes to the fd, so `poll` returns |
| `g_main_context_check` | "Given what `poll` returned, ready now?" | A message is queued |
| `g_main_context_dispatch` | "Run your callback" | Pops the message and emits the bus's `message` signal |

`g_main_context_iteration` is one such turn, and `g_main_loop_run` repeats
it. Only the wait blocks; any other loop can take that part over.

**1. One loop runs the other's sources.** One thread, one wait call, and
the other loop's work is added to it. That can go two ways:

* **Qt waits, GLib is the guest.** Each turn, Qt would call `prepare` and
  `query`, add GLib's fds and deadline to its own wait, block, then call
  `check` and `dispatch`. GLib never waits itself. Qt does not ship this
  (see below for macOS and Windows).
* **GLib waits, Qt is the guest.** This is what `QEventDispatcherGlib`
  does on Linux. Qt turns its own work into GLib sources:
  * posted events: ready while Qt's posted-event list is not empty; a
    post from another thread calls `g_main_context_wakeup`; dispatch is
    `sendPostedEvents()`;
  * timers: `prepare` reports the next timer's deadline, which becomes
    GLib's wait timeout; dispatch fires the expired timers;
  * socket notifiers: their fds go to `query`, including the X11 or
    Wayland connection, which is how input arrives.

  Then `exec()` calls `g_main_context_iteration` turn after turn, and one
  `poll()` waits for input, Qt's timers, posted events and every library
  source on the default context, the bus watch among them.

**2. Each loop gets its own thread.** The loops never meet: Qt's loop keeps
the main thread, and a second thread runs `g_main_loop_run`. Neither needs
to know about the other, but each one's callbacks run on its own thread, so
anything they hand to the other must be thread-safe or queued.

**3. Skip the other loop.** Use a library API that needs no loop: a
callback on the library's own thread that hands off with a queued call (a
sync handler), or polling from the other loop's timer (`gst_bus_pop` from
a `QTimer`).

**Why macOS and Windows cannot do as Linux.** Their GUI thread must block
in the system's own wait, or its windows freeze:
`MsgWaitForMultipleObjectsEx` on Windows, which returns on window messages
or signalled kernel handles, and Cocoa's run loop on macOS. So only "Qt
waits, GLib is the guest" fits, with GLib's fds and deadline translated
into the system's wait each turn. Neither Qt nor GLib provides that glue
([docs/playback.md](../docs/playback.md), "GStreamer bus messages", says
what it would take).

**The same loop as GTK.** On Linux a Qt app and a GTK app run the same
kind of loop: GLib's default `GMainContext`, with the toolkit's display
connection as one of its sources (GDK's for GTK, Qt's for Qt). So a Qt app
can use anything built for GLib in the same process (GIO, a GTK file dialog
through the GTK platform theme, the bus watch) without an extra thread.
Three limits:

* It is shared within one process: each program has its own context.
* Qt still decides when a turn runs: `exec()`, and the nested loops of
  `QDialog::exec()` and `QMenu::exec()`, call `g_main_context_iteration`.
* Only the main thread uses the default context; every other `QThread`
  with a loop gets a `GMainContext` of its own. So GLib sources on the
  default context, such as the bus watch, run on the main thread.

The cost over GTK, a few callbacks per turn and Qt's own dispatch
(`notify`, event filters), is small next to the `poll` call and the
handlers' work; GTK has its own layers too.

### Running GLib's loop

The app uses a different one of the three ways on each platform:

| | Way | Who runs the bus watch | Delivery to `PlaybackController` |
| --- | --- | --- | --- |
| Linux | 1. One loop runs the other's sources (GLib waits, Qt is the guest) | The main thread, through `QEventDispatcherGlib` | Direct, nested inside the bus dispatch |
| macOS, Windows | 2. Each loop gets its own thread | `PlaybackBackendManager`'s `std::thread`, running a `GMainLoop`, started with the GStreamer backend | Queued |
| (planned) | 3. Skip the other loop, with a sync handler | No one: there is no bus watch | Queued, in one hop |
| (rejected) | 3. Skip the other loop, by polling from a `QTimer` | No one | Direct, from the timer's turn |

The result is that a bus message reaches `PlaybackController` by a
different path on each platform:

```
macOS, Windows: two hops (the bus's queue, then Qt's)

streaming thread         GLib thread                      main thread
post end-of-stream ───→  onBusMessage
  (bus queue,              emit mediaStatusChanged ───→    onMediaStatusChanged
   GLib wake-up)           (AutoConnection: the            (queued call)
                            receiver lives on the
                            main thread, so queued)

Linux: one hop (the bus's queue)

streaming thread         main thread
post end-of-stream ───→  onBusMessage
                           emit mediaStatusChanged
                             → onMediaStatusChanged       (direct call, inside
                                                           the bus dispatch)
```

What decides each hop:

* **The bus's queue** (the first hop on macOS and Windows, the only one on
  Linux) lands on whichever thread runs the default `GMainContext`. The
  context is a delivery address like affinity, but GLib's: the player's
  affinity plays no part.
* **Qt's queue** (the second hop on macOS and Windows), and its absence on
  Linux, is affinity: the `AutoConnection` comparison in "A signal across
  threads".

On Linux `onMediaStatusChanged` therefore runs inside `onBusMessage`, which
runs inside GLib's dispatch of the bus watch, all on the main thread's
stack:

```
QApplication::exec()
 └ QEventDispatcherGlib::processEvents
    └ g_main_context_iteration            GLib's loop, as Qt's loop
       └ g_main_context_dispatch
          └ bus watch → the bus's "message" signal
             └ GstAudioPlayer::onBusMessage
                └ emit mediaStatusChanged(EndOfMedia)       direct call
                   └ PlaybackController::onMediaStatusChanged
                      └ next() → setSource → gst_element_set_state(READY)
```

When it starts the next song, the pipeline goes back to `READY` and gets
the next song's URI while the frames below are still active;
`onBusMessage` makes each emit its last statement so nothing runs after it
([docs/event-ordering.md](../docs/event-ordering.md), "When a receiver may
change the sender"). The full description, with the reason for the thread
and its costs, is in [docs/playback.md](../docs/playback.md), "GStreamer
bus messages".

### Planned: one hop with a sync handler

The third way skips GLib's loop: the bus can be read without one. A sync
handler runs on the thread that posts the message, at the moment it posts
it; if the handler does nothing but hand the message on with
`invokeMethod(self, ..., Qt::QueuedConnection)`, the message reaches the
main thread in one hop, the same way on every platform. It is the main
thread because the context object, `self` (the player), lives there, as in
"A queued call from a foreign thread":

```
Every platform: one hop (Qt's queue)

streaming thread                          main thread
post end-of-stream
 → onSyncMessage(msg)
   invokeMethod(self, ..., Queued) ────→  handleBusMessage(msg)
   return GST_BUS_DROP                      emit mediaStatusChanged
                                              → onMediaStatusChanged
                                                (direct call, in its own
                                                 event-loop turn)
```

No `GMainContext` is involved, and the emit that follows is direct because
it runs on the thread `PlaybackController` lives in. What changes compared
with the bus watch:

* **Where the hop is.** Today, on macOS and Windows, the hop to the main
  thread happens at the emit, at the end of `onBusMessage`, through the
  `AutoConnection`. With the sync handler it happens before the switch, so
  the whole switch runs on the main thread.
* **No nesting on Linux.** The emit is still a direct call, but the main
  thread runs it from the posted event, with no bus dispatch below it on
  the stack.
* **No GLib thread.** Nothing waits on the bus queue any more, so
  `PlaybackBackendManager` needs no `GMainLoop`, and a Qt without GLib on
  Linux works too.

Three details follow from how sync handlers work:

* **The handler returns `GST_BUS_DROP`**, meaning "taken, do not queue
  it". `GST_BUS_PASS` would also put the message on the bus's queue, which
  nothing reads, and `GST_BUS_ASYNC` would block the posting thread until
  someone pops it. With `GST_BUS_DROP` the handler owns the message's
  reference, and the queued call releases it when it is done or dropped.
* **It also runs on the main thread**, when the player's own
  `gst_element_set_state` posts a state change. It posts in that case too,
  so even that delivery happens in a later turn.
* **It may only hand off**, like any streaming-thread callback.

A message posted before `setSource` is still delivered after it, as it is
today on macOS and Windows: the hop changes where messages are handled,
not when GStreamer posts them. The gains, costs and teardown order are in
[docs/playback.md](../docs/playback.md), "Alternative: one hop through a
sync handler".

### Positions

The position is not a message. `GstAudioPlayer` asks the pipeline for it
(`gst_element_query_position`) from a 100 ms `QTimer`. The timer is the
player's child, so it lives on the main thread and fires there: the
position is queried and `positionChanged` emitted on the main thread, on
every platform.

## The threads in this app

| Thread | Started by | What runs there | How results reach the main thread |
| --- | --- | --- | --- |
| Main | Qt (`exec()`) | The UI, models, playback, the library, plugins | (it is the main thread) |
| GStreamer streaming threads | GStreamer | Decoding and output; `GstAudioPlayer`'s pad probes | State shared with the main thread behind a mutex or an atomic; bus messages |
| GLib loop (macOS, Windows) | `PlaybackBackendManager` | The bus watch: `GstAudioPlayer::onBusMessage` | Queued signals (`AutoConnection` to a receiver on the main thread) |
| efsw watcher | efsw | File system notifications (`EfswFileSystemMonitor::Listener`) | Queued `invokeMethod` to the monitor |
| Cloud sync worker | `CloudPlayStatsSyncCoordinator` (a `QThread`) | The HTTP calls, in a worker object moved there (its affinity is the sync thread) | Queued `invokeMethod` to the coordinator (tasks reach the worker the same way) |
| `QtConcurrent` pool | `MonitoredLibrary`, `LibraryLoader` | Scans and the library load, on copies of their input | `QFutureWatcher::finished`, on the main thread |
| Artwork preparation | Each system media interface (`WindowsMediaCenter`, `MacOSMediaCenter`, `MprisMediaInterface`), a detached `std::thread` per cover | Scaling the cover to PNG (and on Linux writing it to a file) | Queued `invokeMethod` to the interface, with a task id so an outdated result is dropped |
| SMTC callbacks (Windows) | WinRT | The media overlay's button and seek handlers | Queued `invokeMethod` to `WindowsMediaCenter` |

Data crosses threads in two ways only:

* **Pushed as a queued call.** The other thread posts the work to a context
  object, and the thread that object lives in (the main thread, or the
  sync thread for the cloud worker) runs it in its own turn. The posting
  thread passes the object's pointer and touches nothing else of it. A
  `QtConcurrent` task's result arrives the same way: its `QFutureWatcher`,
  on the main thread, gets `finished` as a posted event.
* **Pulled from shared state.** The other thread writes, the main thread
  reads, and a mutex or an atomic guards it: the PCM tap's samples
  (`PcmTap`, [docs/spectrum.md](../docs/spectrum.md)), the bitrate byte
  count, the ReplayGain tags.

### Rules that follow

* **Do not create `QObject`s on a thread with no Qt loop**: they would live
  there, and their queued calls, timers and `deleteLater` would never run
  ("Thread affinity").
* **A callback on a foreign thread only hands off** (posts, or writes
  guarded state). UI and models are main-thread only.
* **The receiver must outlive the thread that posts to it**, or the thread
  must stop first. Queued calls to a destroyed object are dropped, but a
  thread still running can post to a dangling pointer.
  `EfswFileSystemMonitor` orders its members so the watcher thread stops
  before its listener goes; `PlaybackBackendManager` hands the old player
  to `deleteLater` and joins the GLib thread before that runs;
  `GstAudioPlayer` sets its pipeline to `NULL`, which stops the streaming
  threads, before it releases it.
* **A queued result may be outdated by the time it runs.** The main thread
  may have moved on (another song, another artwork request); the handler
  checks it is still wanted, as the media interfaces' artwork task id does.

## Re-entry and races

Two problems come from this model, both covered with their timing in
[docs/event-ordering.md](../docs/event-ordering.md):

* **Re-entry.** A direct call is synchronous, so a receiver that changes
  the sender does it while the sender is still in the middle of its emit
  (the Linux stack above). The doc lists when that is harmless and the
  app's cases.
* **Races.** A queued call or a timer runs later, and in between other
  work may run: the end of a song, a click. A decision made before the
  wait can be wrong after it. Deferring removes re-entry, not races.

## Where to read next

* [docs/architecture.md](../docs/architecture.md), "Threading / Async
  Model": which subsystem runs on which thread.
* [docs/playback.md](../docs/playback.md), "GStreamer bus messages": the
  two hops, the GLib thread, the rejected designs, their costs, and the
  planned sync handler.
* [docs/event-ordering.md](../docs/event-ordering.md): delivery, re-entry,
  the known races, the plan (a synchronous core with queued edges, plugin
  commands), how foobar2000 and DeaDBeeF order their events.
* [docs/spectrum.md](../docs/spectrum.md): the PCM tap shared between the
  streaming thread and the main thread.
* `src/gstaudioplayer.cpp`: the bus watch (`onBusMessage`) and the pad
  probes; `src/playbackbackendmanager.cpp`: the GLib thread;
  `src/efswfilesystemmonitor.cpp`: a foreign thread handing off.

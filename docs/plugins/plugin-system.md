# Plugin system

## Summary

* A **plugin** is a DLL (a shared library on macOS and Linux) that the app
  loads at startup with `QPluginLoader`. It sees the app only through a
  small API and never uses app classes.
* Each plugin declares an **id** and a display **name** in a metadata JSON
  that moc embeds into its DLL. The host reads both without running any of
  the plugin's code, so a plugin that is not loaded is still listed by name.
* A plugin can contribute several kinds of things, or none (a plugin with no
  UI that only follows playback). Each kind is a **capability**: an
  interface the plugin implements and the host asks for. There are four:
  a **panel** (an element of the main window's layout), a **Settings
  dialog page**, a **Playback
  submenu** and **song fields** (columns whose values the plugin keeps).
  Later kinds (context menu actions) are added the same way, without
  changing the existing interfaces or plugins.
* A plugin follows playback through listener events and controls it
  through **commands**, which the host defers. A plugin that shows the
  playing song (a visualization) reads its decoded **audio** from the host.
* Each plugin gets **its own host object**, with a data folder of its own.
* The user can **disable** a plugin in Settings > Plugins; a disabled plugin
  is not loaded at the next startup. Enabling or disabling takes effect
  after a restart.
* **The plugins are Lyrics ([lyrics.md](lyrics.md)), A-B Repeat
  ([abrepeat.md](abrepeat.md)), Play Stats ([playstats.md](playstats.md))
  and Spectrum ([spectrum.md](spectrum.md)),** each designed in its own doc
  in this folder; this doc is the plugin system they share. Properties stays
  built in.
* Plugins are built in this repository, with the same Qt, compiler and build
  type as the app, and the build puts them next to it. Third-party plugins
  (a published SDK, a stable ABI, a package format with a binary per
  platform for installing a plugin separately) are out of scope.
* A missing plugin, or one that fails to load, contributes nothing; the
  reason is logged and shown on the Plugins page. The app runs without any
  plugin.

## The boundary

`MYLib` is a static library linked into `EXEC`, so a plugin cannot link it:
it would carry its own copy of MYLib's globals. Its `FieldTypePool` would be
empty (the app's `ColumnRegistry` fills only the exe's copy), so
`FieldValue::valueType()` in plugin code would hit `qFatal`
(`fieldvalue.cpp`), and `qobject_cast` would fail on app objects, whose
`staticMetaObject` is the exe's copy.

So a plugin depends on Qt and on `src/pluginapi/` only, as the layout module
(`src/layout/`) depends on Qt only. Qt is shared DLLs, one copy in the process,
so Qt types cross the boundary safely. App types never do: they need app
code the plugin cannot link, they would make every app class plugin API, and
the app passes them by reference into its own state. The host copies what a
plugin needs into plain Qt data instead.

Enforced by the build: `pluginapi` is a CMake `INTERFACE` library (its
include path only). A plugin target links only Qt and `pluginapi`, so its
include path has `src/pluginapi/` but not `src/`, and including an app header
fails to compile.

## Building a plugin: `src/plugins/<name>/`

* Two targets per plugin, added in `src/CMakeLists.txt`
  (`add_subdirectory`):
  * `my<name>` (`mylyrics`, `myabrepeat`, `myplaystats`, `myspectrum`): a
    static library with the plugin's code, linking Qt and `pluginapi` only.
    Built position-independent, as it is linked into a shared library; the
    tests link it directly.
  * `<name>plugin` (`lyricsplugin`, `abrepeatplugin`, `playstatsplugin`,
    `spectrumplugin`): a `MODULE` library linking it, with the root object
    and its metadata JSON. The root object is not in the static library: its
    `Q_PLUGIN_METADATA` would make each test executable linking that library
    a plugin too.
* `myplayer_deploy_plugin` (`src/CMakeLists.txt`) puts the module where the
  app looks (`plugins/` next to `EXEC`, the bundle's `Contents/PlugIns`) and
  in `Tests/plugins`, next to the test executables. `EXEC` and the tests
  that load plugins depend on the modules, so building them builds the
  plugins.

## Plugin API: `src/pluginapi/`

Header-only, Qt types only, no `Q_OBJECT`: a `Q_OBJECT` class in a
header-only API would have its moc code compiled into the app and into every
plugin, two metaobjects for one class.

### Metadata

* A plugin's root class declares
  `Q_PLUGIN_METADATA(IID IPlugin_iid FILE "<name>.json")`, the JSON next to
  its header: `{ "id": "lyrics", "name": "Lyrics" }` for the lyrics plugin
  (`lyricsplugin.json`). moc resolves the file next to the header and
  embeds it, so nothing new is deployed, and AUTOMOC re-runs moc when it
  changes. The JSON is listed in the target's sources for IDEs.
* `id`: stable; names the plugin's panel in the saved layout
  (`layout/tree`), its settings keys (`<id>/...`) and its entry in the
  disabled list. `name`: shown on the Plugins page. Both non-empty
  strings.
* The host reads them from `QPluginLoader::metaData().value("MetaData")`,
  which parses the file without running any of its code. They are declared
  only there, not also in code (see "Rejected").
* `panelTitle()`, `settingsPageTitle()` and a menu's title stay in code:
  they label a capability, can differ from the plugin's name, and are
  translated with `tr()`.

### Core: every plugin

| Type | Implemented by | Role |
|---|---|---|
| `IPlugin` | plugin | `initialize(IPluginHost &)` (once, after loading), `shutdown()` (once, before the host goes) |
| `IPluginHost` | host | The host services, one object per plugin: `player()` returns the `IPlayerHost`, `control()` the `IPlayerControl`, `dataFolder()` the plugin's data folder, `songFieldHost()` the `ISongFieldHost`, `audio()` the `IAudioHost` |
| `IPlayerHost` | host | `addListener(IPlayerListener *)`, `removeListener(IPlayerListener *)` |
| `IPlayerControl` | host | Playback commands: `seek(qint64 ms)`, `play()` |
| `IPlayerListener` | plugin | `trackStarted(const TrackInfo &)`, `stopped()`, `paused()`, `resumed()`, `positionChanged(qint64 ms)`, `durationChanged(qint64 ms)`, `seeked(qint64 ms)`, `finished()` |
| `ISongFieldHost` | host | `valuesChanged(keys)`: the values of a provider's keys changed (see "Provided song fields") |
| `IAudioHost` | host | `setWanted(bool)`, `read(out)`: the playing song's decoded audio (see "Audio for plugins") |
| `TrackInfo` | (plain data) | `fields` (column fields by id, e.g. `title`, `attr:lyrics`, as text); `tagFields` (tag fields with no column, from the refresh on play's `remainingFields`) |

A plugin gets the host in `initialize`, not from a capability, so a plugin
with no UI can follow playback too; it keeps the host for its capabilities
(a lyrics panel's presenter adds itself to `player()`). After `shutdown` it
must not use the host, and by then it has removed every listener it added.

**One host object per plugin.** The data folder, the fields' changes and
whether the plugin wants audio belong to one plugin each, and a host shared
by every plugin could not tell which plugin calls. So `PluginManager` gives each loaded plugin its own
`PluginHost` (in `pluginmanager.cpp`), which knows the plugin's id; its
`player()` and `control()` are the manager's shared `PluginPlayerHost`.
Rejected: passing the plugin's id (or its provider) to each call: a plugin
could pass another's, and every call would repeat what the host knows.

**`dataFolder()`** is `<data root>/<id>/`. `MainWindow` passes the data root
to `PluginManager`: `<AppDataLocation>/plugins`, beside `myplayer.db`; the
tests pass a temporary folder (`MainWindow`'s test constructor and
`tst_plugins`), so no plugin touches the developer's data. The host returns
the path only; the plugin creates the folder on its first write.

### Events: subscription

A plugin subscribes to host events by adding a listener;
`PluginPlayerHost` connects to `PlaybackController`'s signals (an ordinary
`connect`, both app code), turns their app types into plain data (`MSong`
into `TrackInfo`) and calls each listener's method, a virtual call that
needs no moc code or app symbol in the plugin. Inside the plugin, signals
work as usual: its `QObject`s live in the DLL only.

* The listener methods have empty default bodies, so a plugin overrides
  only the events it uses.
* One listener interface per area of the app. Playback (`IPlayerListener`)
  is the only one; another area (library, playlists) gets its own listener,
  reached through a new `IPluginHost` method, when a plugin needs it.
* One way to subscribe, `addListener` / `removeListener`, called by the
  object that owns the listener: a panel's presenter for the panel's
  lifetime (constructor, destructor), a plugin with no UI for the plugin's
  lifetime (`initialize`, `shutdown`).
* A new event in an existing area is a new method on its listener, a core
  API change (see "Versions").
* Delivery is synchronous, on the main thread, in the order the listeners
  were added. A listener must not add or remove listeners during a call:
  `PluginPlayerHost` delivers by looping over its `std::vector` of
  listeners, which adding (a reallocation) or removing (a shift) would
  invalidate mid-loop. Re-entry is in [event-ordering.md](../event-ordering.md).

### Commands: `IPlayerControl`

* `seek` and `play` are the commands; `pause` and others come with a plugin
  that uses them. `play` is the app's Play button (`PlaybackController::play`):
  it resumes a paused song, and with nothing playing starts the shown
  playlist; while playing it changes nothing, so a plugin that wants
  playback running can call it without tracking `paused` and `resumed`.
* Its header states the contract of [event-ordering.md](../event-ordering.md)
  ("Plugin commands: the contract"), except rule 6: deferred, in order,
  allowed inside a listener call, callable from any thread, reported by
  events, not merged, dropped at shutdown. `seek` is not yet bound to a
  session: it applies to whatever plays when it runs, so a seek called just
  before a song change lands in the next song (event-ordering.md, "Known
  races"). The check against the playback generation comes with the
  generation (event-ordering.md, "Plan: A synchronous core with queued
  edges").
* Its own interface, not methods on `IPlayerHost`: listeners and commands
  follow different rules (synchronous calls into the plugin, deferred calls
  into the host), and `IPlayerControl`'s header holds the command contract
  alone.

### Capabilities: what a plugin contributes

| Interface | Role | Used by |
|---|---|---|
| `IPanelProvider` | `panelTitle()` (its title in the layout), `createPanel(QWidget *parent)` | the layout (`MainWindow::setUpLayout`) |
| `ISettingsPageProvider` | `settingsPageTitle()`, `createSettingsPage(QWidget *parent)`, returning an `IPluginSettingsPage` | `SettingsDialog` |
| `IPluginSettingsPage` | `widget()`, `apply()` (called on the Settings dialog's OK) | `SettingsDialog` |
| `IMenuProvider` | `createMenu(QWidget *parent)`, a `QMenu` with its title set | the Playback menu (`MainWindow`) |
| `ISongFieldProvider` | `songFields()`, `keyFields()`, `songKey(...)`, `values(keys)`, `allValues()` | `ColumnRegistry` and `SongLibrary`, through `PluginManager::registerSongFields` |

* The plugin's root object implements `IPlugin` and any capabilities
  (`Q_INTERFACES(IPlugin IPanelProvider ...)`). The place in the app that
  uses a capability asks each loaded plugin for it with `qobject_cast`; a
  plugin without it is skipped there.
* `createPanel`, `createSettingsPage` and `createMenu` return widgets
  parented to `parent`, so the window's UI (the layout's element store, then
  wherever the layout places the panel), the Settings dialog and the
  Playback menu own and delete them. `createPanel` is called once, at
  startup. The plugin keeps its open panels, so
  `apply()` can restyle them.
* A plugin's menu is a submenu, not actions added to Playback, so its items
  show which plugin they come from and several plugins' items do not mix.
  The plugin gets no access to Playback itself, so it cannot change or
  remove the app's items. Only Playback takes plugin menus; another menu
  comes with a plugin that needs it, as a new version of the capability.
  Plugin menu items have no keyboard shortcuts: one could clash with the
  app's or another plugin's, and nothing would arbitrate.
* One panel per plugin, so the plugin's id names its panel in the layout
  (`lyrics` for the lyrics plugin).

### Provided song fields

A plugin's `ISongFieldProvider` declares fields (id, title, `Number` or
`DateTime`, default width and visibility) and the built-in tag fields a key
is computed from. Songs with the same key share values; the key's rule is
the plugin's (`songKey`). The host registers the fields as columns and
expression symbols (`ColumnSource::Plugin`), keeps a copy of the values on
each song, so sorting, filtering and autoplaylists work as for any field,
and reads them again for the keys the plugin reports through
`ISongFieldHost::valuesChanged`, which the host defers. `allValues` serves
the startup load, on the load's workers; `values` serves every refresh
after it. The design, with the Play Stats plugin that uses it, is in
[playstats.md](playstats.md) ("Fields in the host").

### Extending the API

* **A new kind of contribution** (for example `IContextMenuProvider`) is a
  new capability interface, plus the code at the one place in the app that
  uses it. Plugins that do not implement it are unaffected, and the core API
  does not change.
* **A new host service** (for example the selected songs, for a context
  menu) is a new interface returned by a new `IPluginHost` method: a core
  API change.
* The API holds only what its plugins use: what Lyrics, A-B Repeat, Play
  Stats and Spectrum need.

### Versions

* Only interfaces declared with `Q_DECLARE_INTERFACE` have an IID, which
  `qobject_cast` checks: `IPlugin` and the capabilities. `qobject_cast` to an
  interface compares the IID string, not metaobject addresses, so it works
  across the boundary. The host interfaces, the listeners and `TrackInfo` are
  plain classes passed by pointer or reference; nothing checks them, and an
  old plugin calling a changed one would use the wrong function table.
* So `IPlugin`'s IID (`myplayer.IPlugin/1`) is the version of the **core
  API**: `IPlugin`, `IPluginHost`, `IPlayerHost`, `IPlayerControl`,
  `ISongFieldHost`, `IAudioHost`, every listener, `TrackInfo` and the
  metadata keys. A plugin built against
  another version is not loaded; the log names the plugin's IID, which
  `QPluginLoader::metaData()` reads without creating the plugin.
* Each capability's IID (`myplayer.IPanelProvider/1`, ...) is the version of
  that capability and the types only it uses (`IPluginSettingsPage` for
  `ISettingsPageProvider`). A plugin built against another version keeps
  loading, and the capability is not used. This is not logged:
  `qobject_cast` returns null for both "another version" and "not
  implemented", and the host cannot tell them apart.
* **While the API is in development, every version stays at 1**, the core
  API's and each capability's. Plugins are built in this repository with
  the app, so a change reaches every plugin at the next build, and a version
  would count changes no plugin outside the repository sees. Versions start
  counting once plugins are built outside it.
  * Rejected: bumping on every change. Each bump would only rename the IID
    in the API header and the docs, for no plugin that could be refused.
  * Cost: a stale plugin file, built against an older API and left in the
    plugins folder (one the build no longer produces, or a copy from an
    older build), is loaded. Calling an interface that changed uses the
    wrong function table, which usually crashes. Deleting the file fixes
    it.

### Rules

* All calls are on the main thread, except commands and `valuesChanged`,
  which may come from any thread, and a song field provider's `songKey`,
  `values` and `allValues`, which the host may call on the library load's
  workers (the plugin reads off the main thread with connections of its
  own).
* Every host call to a plugin happens between its `initialize` and its
  `shutdown`: `~MainWindow` waits for a running library load before the
  manager shuts the plugins down ([playstats.md](playstats.md),
  "Lifetime").
* **No I/O while plugins load.** `initialize`, `shutdown` and the capability
  calls made while the window is built (`createPanel`, `createMenu`,
  `songFields`, `keyFields`) do no file, database or network I/O; reading
  `QSettings` is allowed (it reads its store once). Work that needs I/O
  starts later: on a listener
  event, from the event loop, on the plugin's own thread, or in a
  provider's reads, which the host makes from the load's workers or the
  event loop.
* The host has no "current track" query: plugins are initialized and panels
  created in the `MainWindow` constructor, before playback can start
  (`setReady` comes after the library load), so every listener sees each song
  start.
* A plugin stores its settings with `QSettings` under `<id>/`, the id its
  metadata declares (the host sets the organization and application names,
  which the plugin's `QSettings` shares through QtCore), as the lyrics
  plugin's `lyrics/font_family`, ...

## Host

### `PluginManager`

Frontend class. `MainWindow` creates it as a child `QObject` after
`ui->setupUi(...)` and `PlaybackController`.

* Takes the plugin folder and the data root (see "`dataFolder()`").
* Lists every library in one folder, in name order. `MainWindow` passes the
  folder: `../PlugIns` when the executable is in a macOS app bundle
  (`.app/Contents/MacOS`), else `plugins/` next to the executable (Windows,
  Linux, and the test executables on every platform). Files that are not
  libraries (a Debug build's `.pdb` and `.ilk` sit next to the plugin) are
  ignored.
* `plugins()` returns a `PluginEntry` per library:

  | Field | Contents |
  | --- | --- |
  | `fileName` | The library's file name, for example `lyricsplugin.dll` |
  | `id`, `name` | From the metadata; empty when it could not be read |
  | `state` | `Loaded`, `Disabled` or `Skipped` |
  | `reason` | Why it was skipped, in a few words (see the steps below) |
  | `details` | The full reason, the text that is logged |
  | `root` | The root object when `Loaded`, else null |

* For each library, in this order (a skip's `reason`, then its `details`;
  where no details are given, they are the reason):
  1. Empty metadata (not a Qt plugin, or another Qt version or build
     type): skipped, "not a plugin for this app", Qt's `errorString()`.
  2. Another IID: skipped, "another plugin API", "built for plugin API
     `<iid>`, the app has `<IPlugin_iid>`".
  3. No `id` or `name`: skipped, "no id or name in its metadata".
  4. An id that an earlier entry, loaded or disabled, already has: skipped,
     "same id as `<file>`". The id names the plugin's panel in the layout,
     its settings keys and its entry in the disabled list.
  5. Its id is in `plugins/disabled`: `Disabled`, not loaded.
  6. `instance()` fails: skipped, "failed to load", Qt's `errorString()`.
     The root object does not implement `IPlugin` (missing from its
     `Q_INTERFACES`): skipped, "not a plugin for this app", "does not
     implement IPlugin".
  7. Otherwise `initialize`, with the plugin's own `PluginHost`: `Loaded`.
* Steps 1 to 5 read only the metadata, before the plugin's code is loaded,
  so a disabled plugin's code never runs.
* `plugins/disabled` is a list of ids. `loadDisabledPluginIds()` and
  `saveDisabledPluginIds()`, declared with `PluginManager`, are the only code
  that reads and writes it: the manager reads it once, at construction, and
  the Plugins page writes it.
* A library's root object is one per process (`QPluginLoader::instance()`
  returns the same object for the same file), so one manager exists at a
  time: the tests create a `MainWindow` after deleting the previous one, and
  each `initialize` follows the previous `shutdown`.
* Owns the `PluginPlayerHost`, the `PluginAudioHost` and the plugins'
  `PluginHost`s (members).
  A `PluginHost`'s `valuesChanged` queues its keys with
  `QMetaObject::invokeMethod` and `Qt::QueuedConnection`, the host as the
  context, and the manager then emits `providedValuesChanged(pluginId,
  keys)` on the main thread.
* `registerSongFields(registry, library)`: each loaded `ISongFieldProvider`'s
  fields into the `ColumnRegistry` and its source into `SongLibrary`
  ([playstats.md](playstats.md), "Registration").
* Its destructor calls `shutdown` on each loaded plugin; the
  `PluginPlayerHost` and `PluginHost` members are destroyed after that, so a
  plugin's listeners are gone before their host, and a `valuesChanged`
  still queued is dropped. Children are destroyed in creation order, so
  the UI and the plugin panels in it are destroyed before the manager.
* Never unloads a plugin: its code must stay mapped while its objects live.

### `PluginPlayerHost`

* Implements `IPlayerHost`: forwards `PlaybackController::songStarted` (as a
  `TrackInfo` built from the `MSong` and its `remainingFields`),
  `playbackStateChanged` (`None`: stop, `Paused`: pause, `Playing`: resume;
  a song start sends `songStarted` instead), `positionChanged`,
  `durationChanged`, `seeking` (before the backend is told) and
  `songFinished` (before Next) to the listeners. The position comes from
  the controller, not the player: a
  backend switch replaces the player, and a direct connection would stay on
  the old one.
* Implements `IPlayerControl` too, as it holds the `PlaybackController`;
  `PluginManager::control()` returns it. `seek` and `play` queue
  `PlaybackController::seek` and `play` with `QMetaObject::invokeMethod` and
  `Qt::QueuedConnection`, with the host as the context, so a command still
  queued when the host is destroyed is dropped. Both go through the one
  queue, so they run in the order they were called.

### Where the capabilities are used

The places using a capability go through the `Loaded` entries.

* **Layout** (`MainWindow::setUpLayout`): each `IPanelProvider`'s panel is
  a layout element named by the entry's `id`, created in the layout's
  element store before the layout is built. The default layout holds no
  plugin panel; the user inserts one from View > Edit Layout, where the
  Insert menu lists it by its `panelTitle()`. A saved layout that
  names a plugin not loaded (disabled, or skipped) keeps its node, shows
  "*Name* is not available" there with the entry's `name`, and saves it
  unchanged, so the panel is back in its place once the plugin is enabled
  ([layout-editing.md](../layout-editing.md)).
* **Settings dialog**: `MainWindow` passes the entries to `SettingsDialog`,
  which it creates each time Preferences opens. Each `ISettingsPageProvider`
  gets a page under Plugins in the dialog's tree
  ([settings-dialog.md](../settings-dialog.md)), in load order; OK calls each
  page's `apply()`.
* **Playback menu** (`MainWindow::setUpPluginMenus`): each
  `IMenuProvider`'s menu at the end of Playback, in load order, after one
  separator; with no provider, neither. The menus are destroyed with the
  menu bar, before the plugins are shut down.
* **Song fields** (`MainWindow` constructor): `registerSongFields` right
  after the manager is created, before the display expressions are parsed
  and the library loads; `providedValuesChanged` goes to
  `SongLibrary::markStale`.

### Plugins page (`PluginsPage`)

* A frontend widget in `src/`, like `LibraryFoldersPage`: a list
  (`plugins_list`) with a row per entry: the name (the file name when the
  metadata has none), the file name and the state. Above it, a note that a
  change takes effect after a restart.
* `Loaded` and `Disabled` rows have a check box, checked for `Loaded`. The
  state column follows the box before OK:

  | Entry | Box | State column |
  | --- | --- | --- |
  | `Loaded` | checked | Enabled |
  | `Loaded` | unchecked | Disabled after restart |
  | `Disabled` | unchecked | Disabled |
  | `Disabled` | checked | Enabled after restart |
  | `Skipped` | none | Not loaded: its `reason` |

* A skipped row's tooltip has its `details`: Qt's `errorString()` names the
  file's path and is too long for the column.
* A library that is not a Qt plugin at all (a dependency put in the folder)
  is a skipped row too, so the folder's contents are all visible.
* `apply()` (on the dialog's OK) saves the ids of the unchecked rows, plus
  the stored ids that no row with a check box has, so a plugin whose file is
  removed, or that is skipped for now, is still disabled when it comes back.
  Cancel leaves the key unchanged.

## Audio for plugins

The playing song's decoded audio, for plugins that show it (the Spectrum).
The tap it is copied into and how the players fill it are in
[playback.md](../playback.md) ("Audio tap").

### `IAudioHost`

`IPluginHost::audio()`, one per plugin (the plugin's `PluginHost`). Main
thread.

* `setWanted(bool)`: whether the plugin wants audio, for example while its
  panel is shown. A state, not a count: the plugin counts once however often
  it says so. The host copies decoded audio only while some plugin wants it.
* `read(std::span<float> out)`: the mono samples, in [-1, 1], ending at the
  estimated play position, the audio being heard now; zeros where there is
  none. Returns the sample rate, or 0 when no audio overlaps (also while no
  plugin wants audio). A plugin calls it on its own frame timer.
* Raw samples, not a finished analysis: every visualization starts from
  them, and the API fixes no analysis.
* Mono, as the tap keeps it. Cost: no plugin can show left and right apart.
* Rejected: `std::shared_ptr<PcmTap>` in the API. `PcmTap` would become
  plugin API, compiled into the app and the plugin with its layout and
  mutex; `read` is one virtual call.

### `PluginAudioHost`

Frontend class, a member of `PluginManager` beside `PluginPlayerHost`. Each
`PluginHost`'s `setWanted` and `read` call it.

* Owns the one `PcmTap`, the count of plugins that want audio and the
  position estimate. `addUser` for the first of them calls
  `PlaybackController::setPcmTap(tap)`; `removeUser` for the last calls
  `setPcmTap(nullptr)`, then `tap->clear()`: withdrawn first, so the player
  does not refill it, and cleared, so the next user starts from new audio.
  With no visualization plugin loaded or shown, no audio is copied.
* One tap for every plugin, not one each: the player holds one tap and would
  push every buffer into each.
* Lifetime: `PlaybackController` is a child of `MainWindow` created before
  `PluginManager`, so it is destroyed first; `PluginAudioHost` does not call
  it from its destructor (the player's `shared_ptr` keeps the tap alive).
  The panels go with the UI, before the controller, so a panel withdrawing
  on hiding (the window closes before it is destroyed) still reaches it.

**The position estimate** (`PlayPositionEstimate`, Qt-free, time passed in).
The players decode ahead of what is heard (the GStreamer probe sees audio
about 300 ms ahead) and report the position only every 100 ms, while a
visualization draws every 33 ms. So `read` ends at the last reported
position plus the time since, while playing. `PluginAudioHost` feeds it
from `PlaybackController`'s `positionChanged`, `songStarted` and
`playbackStateChanged`, timed by a `QElapsedTimer`:

* A report or a state change restarts it from now; it advances only while
  playing, and holds the last report while paused or stopped.
* When reports stop while playing, it runs past the newest audio, so `read`
  returns 0.
* After a seek it reads the old position until the next report (at most
  100 ms); the tap was cleared, so that read finds no audio.
* In the host, not in each plugin: every visualization needs the audio being
  heard, so the estimate is written and tested once. Rejected:
  `read(endUs, out)` with each plugin estimating from its listener events,
  the same timing code in every plugin, since plugins share no code but
  `pluginapi`. Cost: a plugin reads only at "now", and is not told the
  time its samples end at.

## Rejected

* **Link MYLib into the plugin.** Two copies of the app's globals (see "The
  boundary").
* **Make MYLib a shared library.** One copy, but every app class becomes
  plugin API: Windows export macros on each class, and any internal change
  can break a plugin. No boundary left.
* **One plugin interface with a method per kind of contribution** (a
  `createPanel` that may return null, a `contextMenuActions` that may be
  empty, ...). Each new kind would change the interface every plugin
  implements, and, once versions count, bump its version for all of them.
  Capabilities change nothing that exists.
* **Signals in the API** (`Q_OBJECT` host class), or a small shared
  `pluginapi` DLL to hold its moc code once. The first gives two
  metaobjects; the second is one more library to build and deploy for what
  three listener calls do.
* **Versioned derived listeners** (`IPlayerListener2 : IPlayerListener`
  adding events, the host asking each listener for the new version by its
  interface id). Old plugins would keep loading unchanged, which matters only
  for plugins not rebuilt with the app; ours always are, so changing the
  listener is simpler and keeps one listener per area.
* **String-based `connect`** to a host `QObject *` (`SIGNAL(...)`). It needs
  no moc code in the API, since the lookup goes through the object's own
  metaobject, but a typo or a changed signature fails at runtime instead of
  compile time, the argument types must be registered metatypes, and the app
  uses function-pointer `connect` throughout.
* **Static Qt plugins** (`Q_IMPORT_PLUGIN`). Linked into the exe, so nothing
  is loaded at runtime and the DLL problems (folder, build type) stay hidden.
* **Our own component model** (the COM style, as in players such as
  foobar2000): every interface carries an id of our own (a GUID), every
  object answers a `query(id)` method, and each plugin registers the
  services it provides, which the host enumerates by interface across all
  plugins, with reference counting for objects that cross the boundary. It
  needs no Qt for the plugin mechanism and suits many third-party plugins,
  each adding many services. For in-tree plugins built with Qt it repeats
  what Qt already provides: IIDs, `qobject_cast` (generated by moc instead
  of written by hand) and parent ownership. One root object per plugin,
  asked for its capabilities, is enough while each plugin adds only a few
  things.
* **Our own C ABI** (an `extern "C"` entry point, no Qt types across the
  boundary). It would let plugins built with another Qt or compiler load,
  which only matters for third-party plugins; the cost is wrapping strings,
  widgets and settings at every call. Qt's plugin ABI needs a compatible Qt
  and the same build type, which in-tree plugins have.
* **The id and name in code** (`IPlugin::id()`, a `displayName()`, as
  foobar2000's `componentversion` service). They are known only once the
  plugin is loaded, so a disabled, skipped or newly added plugin would show
  its file name, and loading a disabled plugin for its name would run the
  code the user turned off. Keeping `id()` beside the JSON id would be two
  declarations of one id that can disagree.
* **Enabling and disabling without a restart.** Disabling needs no
  unloading: `shutdown()` and removing the plugin's panel and page, with the
  DLL left mapped. But the layout and the dialog would have to add and
  remove panels and pages while running, and `initialize` and `shutdown`
  would become repeatable, a core API change. A restart costs the user
  little for a rare action, and live toggling can come later.
* **The disabled list by file name.** Renaming the file would enable the
  plugin again; the id is stable and is in the metadata.
* **Removing and installing a plugin from the page.** The plugins folder is
  the list of plugins, so removing one means deleting its file. That folder
  is next to the app, often not writable (Program Files, an app bundle); on
  Windows a loaded DLL cannot be deleted, so a removal would wait for the
  next startup; and a bundled plugin comes back with the next build or
  install, so removing it is disabling it. Both come with a per-user plugin
  folder (`QStandardPaths::AppDataLocation`), where Install copies a file
  and Remove deletes it; bundled plugins can then only be disabled.

## Costs

* Plugins must be deployed next to the app (`plugins/`, the bundle's
  `PlugIns`), and on Windows their build type must match the app's: Qt does
  not load a Debug plugin into a Release app.
* Listener registration is manual; a listener that is not removed leaves a
  dangling pointer in the host.
* While the API is in development, a stale plugin file in the folder, built
  against an older API, is loaded and can crash (see "Versions").
* One `TrackInfo` copy of the song's text fields per song start, and a
  virtual call per event: negligible.
* A command takes effect one event-loop turn later, and a plugin seek can
  land in the next song until the generation check ("Commands").
* Plugin menu items have no shortcuts.
* Plugin audio is mono and read only at the estimated play position ("Audio
  for plugins").
* A plugin's name is not translatable: JSON has no `tr()`. A translated app
  would need per-language keys in it.
* Enabling or disabling takes a restart.
* One JSON file per plugin.

## Tests

Each plugin's own tests are in its doc. These cover the API and the host,
through the built plugins in `Tests/plugins`.

* `tst_plugins`: `PluginPlayerHost`'s events (also after a backend
  switch) and commands, deferred and dropped with the host (a listener
  seeking inside `positionChanged` gets the new position only once the
  event loop runs: the dummy player emits it inside `setPosition`, so an
  immediate seek would show); `PluginManager`'s listing (loaded, a
  non-library ignored, a duplicate id skipped, a disabled entry), and
  `registerSongFields` and `valuesChanged`; `PluginAudioHost` (the tap on
  the player while any user is active, withdrawn and cleared after the last,
  `read` at the estimate, paused and playing). That a disabled plugin's code
  is not loaded is not checked: other test functions load it first, and Qt
  never unloads it.
* `tst_playpositionestimate` (no GUI): the estimate advancing while
  playing, held while paused and stopped, restarted by a report or a state
  change.
* `tst_settingsdialog_ui::pluginsPage_stateFollowsBoxAndOkSavesDisabledIds`,
  with entries made by the test (no DLL, so skipped rows are covered too):
  the state column, what OK saves, and a skipped row's reason and tooltip.
* `tst_mainwindow`: a plugin panel in the default and a saved layout; the
  Lyrics page under Plugins applying to its panel on OK; every plugin
  disabled (no panel, page or submenu; a saved Lyrics tab kept "not
  available"). It finds the lyrics and spectrum panels by object name:
  across a DLL, the test's metaobject of a plugin class is not the plugin's,
  so `findChild` by type finds nothing.
* The plugin's `QSettings` uses the tests' QSettings folder: the folder is
  set in QtCore, which the plugin shares.
* By hand: the Plugins page's layout, the state column following the box,
  and that a plugin disabled on OK is gone after a restart and back after
  enabling it and restarting.

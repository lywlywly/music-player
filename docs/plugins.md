# Plan: Plugins

Status: implemented.

## Summary

* A **plugin** is a DLL (a shared library on macOS and Linux) that the app
  loads at startup with `QPluginLoader`. It sees the app only through a
  small API and never uses app classes.
* Each plugin declares an **id** and a display **name** in a metadata JSON
  that moc embeds into its DLL. The host reads both without running any of
  the plugin's code, so a plugin that is not loaded is still listed by name.
* A plugin can contribute several kinds of things, or none (a plugin with no
  UI that only follows playback). Each kind is a **capability**: an
  interface the plugin implements and the host asks for. There are three:
  an **info panel tab**, a **Settings dialog page** and a **Playback
  submenu**. Later kinds (context menu actions) are added the same way,
  without changing the existing interfaces or plugins.
* A plugin follows playback through listener events and controls it
  through **commands**, which the host defers.
* The user can **disable** a plugin in Settings > Plugins; a disabled plugin
  is not loaded at the next startup. Enabling or disabling takes effect
  after a restart.
* **The plugins are Lyrics and A-B Repeat.** Properties and Spectrum stay
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

So a plugin depends on Qt and on `src/pluginapi/` only, the rule
`src/spectrum/` already follows. Qt is shared DLLs, one copy in the process,
so Qt types cross the boundary safely. App types never do: they need app
code the plugin cannot link, they would make every app class plugin API, and
the app passes them by reference into its own state. The host copies what a
plugin needs into plain Qt data instead.

Enforced by the build: `pluginapi` is a CMake `INTERFACE` library (its
include path only). A plugin target links only Qt and `pluginapi`, so its
include path has `src/pluginapi/` but not `src/`, and including an app header
fails to compile.

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
* `id`: stable; names the plugin's info panel tab
  (`infopanel/current_tab`), its settings keys (`<id>/...`) and its entry in
  the disabled list. `name`: shown on the Plugins page. Both non-empty
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
| `IPluginHost` | host | The host services: `player()` returns the `IPlayerHost`, `control()` the `IPlayerControl` |
| `IPlayerHost` | host | `addListener(IPlayerListener *)`, `removeListener(IPlayerListener *)` |
| `IPlayerControl` | host | Playback commands: `seek(qint64 ms)` |
| `IPlayerListener` | plugin | `trackStarted(const TrackInfo &)`, `stopped()`, `positionChanged(qint64 ms)` |
| `TrackInfo` | (plain data) | `fields` (column fields by id, e.g. `title`, `attr:lyrics`, as text); `tagFields` (tag fields with no column, from the refresh on play's `remainingFields`) |

A plugin gets the host in `initialize`, not from a capability, so a plugin
with no UI can follow playback too; it keeps the host for its capabilities
(a lyrics panel's presenter adds itself to `player()`). After `shutdown` it
must not use the host, and by then it has removed every listener it added.

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
  invalidate mid-loop. Re-entry is in [event-ordering.md](event-ordering.md).

### Commands: `IPlayerControl`

* `seek` is the only command; `play`, `pause` and others come with a plugin
  that uses them.
* Its header states the contract of [event-ordering.md](event-ordering.md)
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
| `IPanelProvider` | `panelTitle()` (tab text), `createPanel(QWidget *parent)` | the info panel (`MainWindow`) |
| `ISettingsPageProvider` | `settingsPageTitle()`, `createSettingsPage(QWidget *parent)`, returning an `IPluginSettingsPage` | `SettingsDialog` |
| `IPluginSettingsPage` | `widget()`, `apply()` (called on the Settings dialog's OK) | `SettingsDialog` |
| `IMenuProvider` | `createMenu(QWidget *parent)`, a `QMenu` with its title set | the Playback menu (`MainWindow`) |

* The plugin's root object implements `IPlugin` and any capabilities
  (`Q_INTERFACES(IPlugin IPanelProvider ...)`). The place in the app that
  uses a capability asks each loaded plugin for it with `qobject_cast`; a
  plugin without it is skipped there.
* `createPanel`, `createSettingsPage` and `createMenu` return widgets
  parented to `parent`, so the tab widget, the Settings dialog and the
  Playback menu own and delete them. The plugin keeps its open panels, so
  `apply()` can restyle them.
* A plugin's menu is a submenu, not actions added to Playback, so its items
  show which plugin they come from and several plugins' items do not mix.
  The plugin gets no access to Playback itself, so it cannot change or
  remove the app's items. Only Playback takes plugin menus; another menu
  comes with a plugin that needs it, as a new version of the capability.
  Plugin menu items have no keyboard shortcuts: one could clash with the
  app's or another plugin's, and nothing would arbitrate.
* One panel per plugin, so the plugin's id names its tab in
  `infopanel/current_tab` (`lyrics` for the lyrics plugin).

### Extending the API

* **A new kind of contribution** (for example `IContextMenuProvider`) is a
  new capability interface, plus the code at the one place in the app that
  uses it. Plugins that do not implement it are unaffected, and the core API
  does not change.
* **A new host service** (for example the selected songs, for a context
  menu) is a new interface returned by a new `IPluginHost` method: a core
  API change.
* The API holds only what its plugins use: what lyrics and A-B Repeat
  need.

### Versions

* Only interfaces declared with `Q_DECLARE_INTERFACE` have an IID, which
  `qobject_cast` checks: `IPlugin` and the capabilities. `qobject_cast` to an
  interface compares the IID string, not metaobject addresses, so it works
  across the boundary. The host interfaces, the listeners and `TrackInfo` are
  plain classes passed by pointer or reference; nothing checks them, and an
  old plugin calling a changed one would use the wrong function table.
* So `IPlugin`'s IID (`myplayer.IPlugin/1`) is the version of the **core
  API**: `IPlugin`, `IPluginHost`, `IPlayerHost`, `IPlayerControl`, every
  listener, `TrackInfo` and the metadata keys. A plugin built against
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

* All calls are on the main thread, except commands, which may come from
  any thread.
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
     "same id as `<file>`". The id names the plugin's info panel tab, its
     settings keys and its entry in the disabled list.
  5. Its id is in `plugins/disabled`: `Disabled`, not loaded.
  6. `instance()` fails: skipped, "failed to load", Qt's `errorString()`.
     The root object does not implement `IPlugin` (missing from its
     `Q_INTERFACES`): skipped, "not a plugin for this app", "does not
     implement IPlugin".
  7. Otherwise `initialize`: `Loaded`.
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
* Implements `IPluginHost`; owns the `PluginPlayerHost` (a member).
* Its destructor calls `shutdown` on each loaded plugin; the
  `PluginPlayerHost` member is destroyed after that, so a plugin's listeners
  are gone before their host. Children are destroyed in creation order, so
  the info panel and the plugin panels in it are destroyed before the
  manager.
* Never unloads a plugin: its code must stay mapped while its objects live.

### `PluginPlayerHost`

* Implements `IPlayerHost`: forwards `PlaybackController::songStarted` (as a
  `TrackInfo` built from the `MSong` and its `remainingFields`),
  `playbackStateChanged(None)` (stop) and `positionChanged` to the
  listeners. The position comes from the controller, not the player: a
  backend switch replaces the player, and a direct connection would stay on
  the old one.
* Implements `IPlayerControl` too, as it holds the `PlaybackController`;
  `PluginManager::control()` returns it. `seek` queues
  `PlaybackController::seek` with `QMetaObject::invokeMethod` and
  `Qt::QueuedConnection`, with the host as the context, so a seek still
  queued when the host is destroyed is dropped.

### Where the capabilities are used

The places using a capability go through the `Loaded` entries.

* **Info panel** (`MainWindow`): a tab per `IPanelProvider`, before the
  built-in tabs, in load order, named in `infopanel/current_tab` by the
  entry's `id`, and created before `setUpInfoTabs()` restores that key.
  When the key names a disabled plugin, the info panel shows its first tab
  and leaves the key as it is (it is written only when the user changes
  tabs), so the plugin's tab is current again once it is enabled.
* **Settings dialog**: `MainWindow` passes the entries to `SettingsDialog`,
  which it creates each time Preferences opens. Each `ISettingsPageProvider`
  gets a page under Plugins in the dialog's tree
  ([settings-dialog.md](settings-dialog.md)), in load order; OK calls each
  page's `apply()`.
* **Playback menu** (`MainWindow::setUpPluginMenus`): each
  `IMenuProvider`'s menu at the end of Playback, in load order, after one
  separator; with no provider, neither. The menus are destroyed with the
  menu bar, before the plugins are shut down.

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

## Lyrics plugin: `src/plugins/lyrics/`

* `mylyrics`, a static library depending on Qt and `src/pluginapi/` only:
  `LyricsPanel` (+ `.ui`), `LyricsManager`, `LyricsLoader`,
  `LyricsPresenter`, `LyricsSettingsPage`, and `loadLyricsSettings` /
  `saveLyricsSettings` (the `lyrics/*` keys, for the presenter and the
  page). Built position-independent, as it is linked into a shared library;
  the tests link it directly.
* `lyricsplugin`, a `MODULE` library linking `mylyrics`: the root object
  `LyricsPlugin`, implementing `IPlugin`, `IPanelProvider` and
  `ISettingsPageProvider`, and its metadata, `lyricsplugin.json`. The build
  (`myplayer_deploy_plugin` in `src/CMakeLists.txt`) puts it where the app
  looks (`plugins/` next to `EXEC`, the bundle's `Contents/PlugIns`) and in
  `Tests/plugins`, next to the test executables; `EXEC` and the tests that
  load it depend on it, so building them builds it. The root object is not
  in `mylyrics`: its `Q_PLUGIN_METADATA` would make each test executable
  linking `mylyrics` a plugin too.
* `createPanel` creates a `LyricsPanel` (object name `lyricsPanel`) and its
  `LyricsPresenter`, a child of the panel. The presenter implements
  `IPlayerListener`, adds itself in its constructor and removes itself in its
  destructor, so it listens for the panel's lifetime, while the host exists.
  The plugin keeps the presenters (`QPointer`s, as the panels are deleted by
  the tab widget).
* `LyricsPresenter` owns the `LyricsManager`, shows the playing song's lyrics
  in its `LyricsPanel` and applies the style settings to it. `LyricsManager`
  holds the lyrics and moves the current line with the play position.
* `LyricsPanel` keeps the index of its current line and re-centers on it,
  without animation, in `showEvent` and `resizeEvent`: the tab keeps getting
  positions while hidden, but a `QTabWidget` sizes only its current page, so
  the hidden panel can be resized later ([info-panel.md](info-panel.md)).

### Where lyrics come from (`LyricsLoader`)

* On play, `PlaybackController` refreshes the song's metadata from its file
  once, so the `TrackInfo` has the file's current tags.
* `LyricsLoader::resolveLyrics` picks the first non-empty of `fields`
  (`attr:unsynced_lyrics`, `attr:lyrics`), then of `tagFields`
  (`unsynced_lyrics`, `lyrics`). If none is set, or it has no timestamped
  lines, the `.lrc` file is the fallback.
* The `.lrc` fallback (`LyricsLoader::getLyrics`) opens
  `<home>/Music/<artist> - <title>.lrc` only, where `<home>` is `USERPROFILE`
  on Windows and `HOME` elsewhere; it does not look next to the audio file.
* Known issue (Windows): the `.lrc` file name is built as a UTF-8
  `std::string` and converted to `std::filesystem::path`, which on Windows
  decodes narrow strings with the active ANSI code page, not UTF-8. A
  non-ASCII artist or title (for example CJK) maps to a different file name,
  so its `.lrc` is not found. macOS and Linux are unaffected, as is Windows
  with the system-wide UTF-8 code page. Fix: build the path from UTF-8
  explicitly (`std::u8string` or a wide path), as `SongParser` does for
  TagLib with `MultiByteToWideChar`.

### Settings (`LyricsSettingsPage`, `lyricssettings.h`)

* Four keys, read and written only by `loadLyricsSettings` /
  `saveLyricsSettings`: `lyrics/use_system_default_font`,
  `lyrics/font_family`, `lyrics/font_size` and `lyrics/highlight_color`.
  Defaults: the system font unless a family is stored, the application
  font's size, and highlight `#0064ff` (also used for a stored color that is
  not valid).
* `font_family` is always stored, even while the system font is used. With
  the system font on, the font combo is disabled but still shows the stored
  family.
* On page load, the combo selects the stored family, or the font Qt
  substitutes for it when it is not installed. The combo is editable, and OK
  stores its text as typed: an unknown or empty family leaves the
  application font's families in effect.
* `highlight_color` is picked with a color dialog and colors the current
  line.
* `apply()` (on the Settings dialog's OK) writes the keys and sends
  `applied`, on which the plugin re-applies them to the open panels.
* The lyrics font is the application font at the stored size. With the
  system font off, the stored family is put first in its family list, so the
  application font's CJK fallback families ([ui-styling.md](ui-styling.md))
  still cover characters the chosen font lacks.

## A-B Repeat plugin: `src/plugins/abrepeat/`

Loops a section of the playing song: the user marks A and B in a submenu of
Playback, and playback jumps back to A each time it reaches B.

| Item | Does |
| --- | --- |
| Set A | Marks the current position as A and clears B |
| Set B | Marks the current position as B; enabled once A is set. Playback then loops |
| Clear | Removes A and B; enabled once A is set |

* The items show the marked times ("Set A (1:23)"), so the loop's state is
  visible without a widget of its own.
* While A and B are set, a position at or past B seeks to A. A seek past B
  (with the slider) jumps back to A at the next position; a seek before A
  plays on until B.
* Set B at or before A does nothing: B is marked while listening past A, so
  this is rare. Swapping A and B instead would make a loop the user did not
  mark.
* A song start (the same song again too) and a stop clear A and B; they are
  not saved. The items are disabled while no song is loaded: before the
  first song starts and after a stop.
* Three items, not one cycling through Set A, Set B and Clear: what that
  item did next would depend on a state the menu has to spell out, and
  marking A again in a set loop would take a Clear first.

### Code

* `myabrepeat`, a static library depending on Qt and `src/pluginapi/` only,
  like `mylyrics`: `ABRepeat`, the loop and its `IPlayerListener`, no UI.
  It holds A, B, the last position and whether a song is loaded. A position
  at or past B calls `seek(A)`; `trackStarted` and `stopped` clear A and B.
* `abrepeatplugin`, a `MODULE` library linking `myabrepeat`, built and
  deployed like `lyricsplugin`: the root object `ABRepeatPlugin` (`IPlugin`,
  `IMenuProvider`) and its metadata, `abrepeatplugin.json`
  (`{ "id": "abrepeat", "name": "A-B Repeat" }`).
* `initialize` creates the `ABRepeat` with the host's `IPlayerControl` and
  adds it as a listener; `shutdown` removes and destroys it.
* `createMenu` builds the submenu (object name `abRepeatMenu`). Its
  `aboutToShow` sets the items' text and enabled state from the `ABRepeat`,
  so the menu needs no notification from it.
* Set A and Set B use the last position the listener received, as the host
  has no position query; the error this adds is in "Precision".

### Precision

About 0.1 s with GStreamer, with a gap at each jump:

* **Marks**: A and B are the last position received, up to one position
  update (100 ms) before the click.
* **Reaching B**: the loop seeks on the first position at or past B, so it
  plays up to 100 ms past B. The position is the audio output's, and a
  flushing seek drops the buffered audio, so the overshoot is what is heard.
* **Landing at A**: `GstAudioPlayer::setPosition` seeks accurately
  ([playback.md](playback.md), "Seeking"), to the exact position, except
  in a VBR MP3 without a seek index, whose position is estimated.
* **The jump**: the flushing seek restarts the output, a short gap or click.
* With Qt Multimedia, Qt's media backend sets how often the position
  arrives, and seeks on macOS are inaccurate (roadmap.md Known Issues).
* **Near the end of a song**: with B in the last position update (100 ms
  with GStreamer), the end-of-stream can come before a position at or past
  B, and the next song plays. Or the position reporting B and the
  end-of-stream are pending in the same event-loop turn, and the queued
  seek lands in the next song ([event-ordering.md](event-ordering.md),
  "Known races").

Options to tighten it, not planned:

* **A timer for B in the plugin**: a position within one update of B starts
  a single-shot `Qt::PreciseTimer` for the rest, which seeks when it fires.
  The overshoot drops to the timer's and the position's error, a few
  milliseconds, with no API change.
* **A gapless loop in the backend**: a segment seek from A to B, seeking
  again on GStreamer's segment-done message, loops sample-exact without a
  gap. It is a loop command for the backend, not a plugin `seek`, and Qt
  Multimedia has no equivalent.

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
  unloading: `shutdown()` and removing the plugin's tab and page, with the
  DLL left mapped. But the info panel and the dialog would have to add and
  remove tabs and pages while running, and `initialize` and `shutdown`
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
* The A-B loop's precision is about 0.1 s, with a gap at each jump (A-B
  Repeat plugin, "Precision").
* Plugin tabs always come before the built-in ones until those are plugins
  too.
* A plugin's name is not translatable: JSON has no `tr()`. A translated app
  would need per-language keys in it.
* Enabling or disabling takes a restart.
* One JSON file per plugin.

## Tests

* `tst_lyrics`, `tst_lyricspanel`: link `mylyrics`; `resolveLyrics` cases
  take `TrackInfo`.
* `tst_abrepeat`, linking `myabrepeat`, with an `IPlayerControl` made by the
  test that records seeks: a position at or past B seeks to A, one before B
  does not; Set B at or before A is ignored; Set A clears B; a song start
  (the same song again too) and a stop clear the loop.
* `tst_plugins`:
  * `PluginPlayerHost` forwards song start (with its tag fields), position
    (also from the player a backend switch creates) and stop, not pause, to
    a listener the test adds, and nothing after it is removed.
  * A listener seeking inside `positionChanged`: the next listener gets the
    old position first, and the new one once the event loop runs
    (`QTRY_COMPARE`). The dummy player emits the position inside
    `setPosition`, so a seek run at once would show as the new position
    first.
  * A seek queued by a host that is then destroyed never runs.
  * `PluginManager` lists the built lyrics plugin from `Tests/plugins` as
    `Loaded`, with the id `lyrics`, the name `Lyrics` and its file name, and
    with a panel and a settings page. The panel shows the tag lyrics of the
    song that starts and clears on stop.
  * The lyrics settings: an open panel's font puts the stored family before
    the application font's families; the page shows the stored size, system
    font choice and color; the system font disables the family combo;
    `apply()` writes the four `lyrics/*` keys (the family as typed) and
    restyles the open panel. Which font the combo selects for a stored
    family is Qt's substitution, checked by hand: the offscreen platform has
    no fonts.
  * `PluginManager` does not list a file that is not a library, and of two
    copies of the plugin the second is `Skipped`, with a reason naming the
    first.
  * With `lyrics` in `plugins/disabled`, its entry is `Disabled` with its
    name and no root object. That its code is not loaded is not checked:
    other test functions load it first, and Qt never unloads it.
* `tst_settingsdialog_ui::pluginsPage_stateFollowsBoxAndOkSavesDisabledIds`,
  with entries made by the test (no DLL, so skipped rows are covered too):
  the state column follows the box; OK writes the unchecked ids and keeps
  the stored ids that no row with a check box has; a skipped row has no
  check box, its `reason` and its `details` as its tooltip.
* `tst_mainwindow`:
  * `infoTabs_restoreAndShowSelectedSong` checks the Lyrics (through the
    plugin in `Tests/plugins`), Properties and Spectrum tabs and the
    restored tab, and that the Lyrics tab is saved as `lyrics`.
  * `settings_pluginPageAppliesToItsPanelOnOk` finds the Lyrics page under
    Plugins in Preferences, and checks that its size reaches the panel on
    OK. It finds the lyrics panel by object name: across a DLL, the test's
    `LyricsPanel` metaobject is not the plugin's, so
    `findChild<LyricsPanel *>` (or `<LyricsManager *>`) finds nothing, and
    the panel's lines are checked in `tst_plugins` instead.
  * `plugins_disabledPluginsHaveNoTabPageOrMenu`: with `lyrics` and
    `abrepeat` in `plugins/disabled` and `infopanel/current_tab` set to
    `lyrics`, no Lyrics info tab, the first tab current and the key
    unchanged, no page under Plugins, and no A-B Repeat submenu or separator
    in Playback.
  * `abRepeatMenu_loopsThePlayingSong`: the A-B Repeat submenu is last in
    Playback, its items disabled with no song; with a song playing, Set A
    and Set B show their times, and a position past B seeks the player to
    A. Items are triggered after the menu's `aboutToShow`, as when the user
    opens it: a disabled action does not trigger.
* The plugin's `QSettings` uses the tests' QSettings folder: the folder is
  set in QtCore, which the plugin shares.
* By hand: the Plugins page's layout, the state column following the box,
  and that a plugin disabled on OK is gone after a restart and back after
  enabling it and restarting. An A-B loop with GStreamer and with Qt
  Multimedia; the submenu's text and enabled state in the macOS native menu
  bar; B near a song's end.

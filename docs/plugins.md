# Plan: Plugins

Status: implemented.

## Summary

* A **plugin** is a DLL (a shared library on macOS and Linux) that the app
  loads at startup with `QPluginLoader`. It sees the app only through a
  small API and never uses app classes.
* A plugin can contribute several kinds of things, or none (a plugin with no
  UI that only follows playback). Each kind is a **capability**: an
  interface the plugin implements and the host asks for. There are two: an
  **info panel tab** and a **Settings dialog page**. Later kinds (context
  menu actions, main menu commands) are added the same way, without changing
  the existing interfaces or plugins.
* **Lyrics is the first and, for now, only plugin.** Properties and Spectrum
  stay built in.
* Plugins are built in this repository, with the same Qt, compiler and build
  type as the app, and the build puts them next to it. Third-party plugins
  (a published SDK, a stable ABI, a package format with a binary per
  platform for installing a plugin separately) are out of scope.
* A missing plugin, or one that fails to load, contributes nothing; the
  reason is logged. The app runs without any plugin.

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

### Core: every plugin

| Type | Implemented by | Role |
|---|---|---|
| `IPlugin` | plugin | `id()` (stable, e.g. `lyrics`; names the plugin in logs, its tab and its settings), `initialize(IPluginHost &)` (once, after loading), `shutdown()` (once, before the host goes) |
| `IPluginHost` | host | The host services: `player()` returns the `IPlayerHost` |
| `IPlayerHost` | host | `addListener(IPlayerListener *)`, `removeListener(IPlayerListener *)` |
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
* A new event in an existing area is a new method on its listener, with a
  core API version bump (see "Versions").
* Delivery is synchronous, on the main thread, in the order the listeners
  were added. A listener must not add or remove listeners during a call
  (the rule for backend signal handlers, which must not mutate the sender).

### Capabilities: what a plugin contributes

| Interface | Role | Used by |
|---|---|---|
| `IPanelProvider` | `panelTitle()` (tab text), `createPanel(QWidget *parent)` | the info panel (`MainWindow`) |
| `ISettingsPageProvider` | `settingsPageTitle()`, `createSettingsPage(QWidget *parent)`, returning an `IPluginSettingsPage` | `SettingsDialog` |
| `IPluginSettingsPage` | `widget()`, `apply()` (called on the Settings dialog's OK) | `SettingsDialog` |

* The plugin's root object implements `IPlugin` and any capabilities
  (`Q_INTERFACES(IPlugin IPanelProvider ...)`). The place in the app that
  uses a capability asks each plugin for it with `qobject_cast`; a plugin
  without it is skipped there.
* `createPanel` and `createSettingsPage` return widgets parented to
  `parent`, so the tab widget and the Settings dialog own and delete them.
  The plugin keeps its open panels, so `apply()` can restyle them.
* One panel per plugin, so the plugin's `id()` names its tab in
  `infopanel/current_tab` (`lyrics` for the lyrics plugin).

### Extending the API

* **A new kind of contribution** (for example `IContextMenuProvider`) is a
  new capability interface, plus the code at the one place in the app that
  uses it. Plugins that do not implement it are unaffected, and the core API
  does not change.
* **A new host service** (for example the selected songs, for a context
  menu) is a new interface returned by a new `IPluginHost` method: a core
  API change.
* The API holds only what its plugins use: it starts with what lyrics needs.

### Versions

* Only interfaces declared with `Q_DECLARE_INTERFACE` have an IID, which
  `qobject_cast` checks: `IPlugin` and the capabilities. `qobject_cast` to an
  interface compares the IID string, not metaobject addresses, so it works
  across the boundary. The host interfaces, the listeners and `TrackInfo` are
  plain classes passed by pointer or reference; nothing checks them, and an
  old plugin calling a changed one would use the wrong function table.
* So `IPlugin`'s IID (`myplayer.IPlugin/1`) is the version of the **core
  API**: `IPlugin`, `IPluginHost`, `IPlayerHost`, every listener and
  `TrackInfo`. Any change to one of them bumps it, and a plugin built
  against another version is not loaded. The log names the plugin's IID,
  which `QPluginLoader::metaData()` reads without creating the plugin.
* Each capability's IID (`myplayer.IPanelProvider/1`, ...) is the version of
  that capability and the types only it uses (`IPluginSettingsPage` for
  `ISettingsPageProvider`). A plugin built against another version keeps
  loading, and the capability is not used. This is not logged:
  `qobject_cast` returns null for both "another version" and "not
  implemented", and the host cannot tell them apart.
* Plugins are built with the app, so a bump means rebuilding them, with no
  source change unless they use what changed.

### Rules

* All calls are on the main thread.
* The host has no "current track" query: plugins are initialized and panels
  created in the `MainWindow` constructor, before playback can start
  (`setReady` comes after the library load), so every listener sees each song
  start.
* A plugin stores its settings with `QSettings` under `<id>/` (the host sets
  the organization and application names, which the plugin's `QSettings`
  shares through QtCore), as the lyrics plugin's `lyrics/font_family`, ...

## Host

### `PluginManager`

Frontend class. `MainWindow` creates it as a child `QObject` after
`ui->setupUi(...)` and `PlaybackController`.

* Loads every library in one folder, in name order. `MainWindow` passes the
  folder: `../PlugIns` when the executable is in a macOS app bundle
  (`.app/Contents/MacOS`), else `plugins/` next to the executable (Windows,
  Linux, and the test executables on every platform).
* Ignores files that are not libraries (a Debug build's `.pdb` and `.ilk`
  sit next to the plugin). Skips, and logs, a library whose IID is not
  `IPlugin`'s (read with `QPluginLoader::metaData()`, before the plugin's
  code is loaded), one that fails to load, and a plugin whose `id()` is
  already loaded: the id names its tab and its settings keys.
* Calls `initialize` on each. `plugins()` returns their root objects
  (`QObject *`), which the places using a capability `qobject_cast`.
* A library's root object is one per process (`QPluginLoader::instance()`
  returns the same object for the same file), so one manager exists at a
  time: the tests create a `MainWindow` after deleting the previous one, and
  each `initialize` follows the previous `shutdown`.
* Implements `IPluginHost`; owns the `PluginPlayerHost` (a member).
* Its destructor calls `shutdown` on each plugin; the `PluginPlayerHost`
  member is destroyed after that, so a plugin's listeners are gone before
  their host. Children are destroyed in creation order, so the info panel
  and the plugin panels in it are destroyed before the manager.
* Never unloads a plugin: its code must stay mapped while its objects live.

### `PluginPlayerHost`

* Implements `IPlayerHost`: forwards `PlaybackController::songStarted` (as a
  `TrackInfo` built from the `MSong` and its `remainingFields`),
  `playbackStateChanged(None)` (stop) and `positionChanged` to the
  listeners. The position comes from the controller, not the player: a
  backend switch replaces the player, and a direct connection would stay on
  the old one.

### Where the capabilities are used

* **Info panel** (`MainWindow`): a tab per `IPanelProvider`, before the
  built-in tabs, in load order, and before `setUpInfoTabs()` restores
  `infopanel/current_tab`.
* **Settings dialog**: `MainWindow` passes `plugins()` to `SettingsDialog`,
  which it creates each time Preferences opens. The dialog adds a tab per
  `ISettingsPageProvider`, before Display, and calls each page's `apply()`
  in `applySettings` (on OK).

## Lyrics plugin: `src/plugins/lyrics/`

* `mylyrics`, a static library depending on Qt and `src/pluginapi/` only:
  `LyricsPanel` (+ `.ui`), `LyricsManager`, `LyricsLoader`,
  `LyricsPresenter`, `LyricsSettingsPage`, and `loadLyricsSettings` /
  `saveLyricsSettings` (the `lyrics/*` keys, for the presenter and the
  page). Built position-independent, as it is linked into a shared library;
  the tests link it directly.
* `lyricsplugin`, a `MODULE` library linking `mylyrics`: the root object
  `LyricsPlugin`, implementing `IPlugin`, `IPanelProvider` and
  `ISettingsPageProvider`. The build puts it where the app looks (`plugins/`
  next to `EXEC`, the bundle's `Contents/PlugIns`) and in `Tests/plugins`,
  next to the test executables; `EXEC` and the tests that load it depend on
  it, so building them builds it. The root object is not in `mylyrics`: its
  `Q_PLUGIN_METADATA` would make each test executable linking `mylyrics` a
  plugin too.
* `LyricsLoader::resolveLyrics` reads `TrackInfo`: the first non-empty of
  `fields` (`attr:unsynced_lyrics`, `attr:lyrics`), then of `tagFields`
  (`unsynced_lyrics`, `lyrics`), then the `.lrc` fallback.
* `createPanel` creates a `LyricsPanel` (object name `lyricsPanel`) and its
  `LyricsPresenter`, a child of the panel. The presenter implements
  `IPlayerListener`, adds itself in its constructor and removes itself in its
  destructor, so it listens for the panel's lifetime, while the host exists.
  The plugin keeps the presenters (`QPointer`s, as the panels are deleted by
  the tab widget).
* `LyricsSettingsPage` writes the `lyrics/*` keys on `apply()` and sends
  `applied`, on which the plugin re-applies the settings to the open panels.

## Rejected

* **Link MYLib into the plugin.** Two copies of the app's globals (see "The
  boundary").
* **Make MYLib a shared library.** One copy, but every app class becomes
  plugin API: Windows export macros on each class, and any internal change
  can break a plugin. No boundary left.
* **One plugin interface with a method per kind of contribution** (a
  `createPanel` that may return null, a `contextMenuActions` that may be
  empty, ...). Each new kind would change the interface every plugin
  implements, and bump its version for all of them. Capabilities change
  nothing that exists.
* **Signals in the API** (`Q_OBJECT` host class), or a small shared
  `pluginapi` DLL to hold its moc code once. The first gives two
  metaobjects; the second is one more library to build and deploy for what
  three listener calls do.
* **Versioned derived listeners** (`IPlayerListener2 : IPlayerListener`
  adding events, the host checking each listener with `dynamic_cast`). Old
  plugins would keep loading unchanged, which matters only for plugins not
  rebuilt with the app; ours always are, so changing the listener and bumping
  the core version is simpler and keeps one listener per area.
* **String-based `connect`** to a host `QObject *` (`SIGNAL(...)`). It needs
  no moc code in the API, since the lookup goes through the object's own
  metaobject, but a typo or a changed signature fails at runtime instead of
  compile time, the argument types must be registered metatypes, and the app
  uses function-pointer `connect` throughout.
* **Static Qt plugins** (`Q_IMPORT_PLUGIN`). Linked into the exe, so nothing
  is loaded at runtime and the DLL problems (folder, build type) stay hidden.
* **Our own C ABI** (an `extern "C"` entry point, no Qt types across the
  boundary). It would let plugins built with another Qt or compiler load,
  which only matters for third-party plugins; the cost is wrapping strings,
  widgets and settings at every call. Qt's plugin ABI needs a compatible Qt
  and the same build type, which in-tree plugins have.

## Costs

* Plugins must be deployed next to the app (`plugins/`, the bundle's
  `PlugIns`), and on Windows their build type must match the app's: Qt does
  not load a Debug plugin into a Release app.
* Listener registration is manual; a listener that is not removed leaves a
  dangling pointer in the host.
* A plugin built against another capability version silently loses that
  capability (see "Versions"); with plugins rebuilt with the app this
  happens only with a stale plugin file in the folder.
* One `TrackInfo` copy of the song's text fields per song start, and a
  virtual call per event: negligible.
* Plugin tabs always come before the built-in ones until those are plugins
  too.

## Tests

* `tst_lyrics`, `tst_lyricspanel`: link `mylyrics`; `resolveLyrics` cases
  take `TrackInfo`.
* `tst_plugins`:
  * `PluginPlayerHost` forwards song start (with its tag fields), position
    (also from the player a backend switch creates) and stop, not pause, to
    a listener the test adds, and nothing after it is removed.
  * `PluginManager` loads the built lyrics plugin from `Tests/plugins`: one
    plugin, `lyrics`, with a panel and a settings page. The panel shows the
    tag lyrics of the song that starts and clears on stop; the page's
    `apply()` writes the `lyrics/*` keys.
  * `PluginManager` ignores a file that is not a library, and of two copies
    of the plugin loads one: the second has the same id.
* `tst_mainwindow::infoTabs_restoreAndShowSelectedSong` checks the Lyrics
  (through the plugin in `Tests/plugins`), Properties and Spectrum tabs and
  the restored tab, and that the Lyrics tab is saved as `lyrics`.
  `settings_pluginPageAppliesToItsPanelOnOk` finds the Lyrics page before
  Display in Preferences, and checks that its size reaches the panel on OK. It finds the lyrics panel by object name: across a DLL,
  the test's `LyricsPanel` metaobject is not the plugin's, so
  `findChild<LyricsPanel *>` (or `<LyricsManager *>`) finds nothing, and the
  panel's lines are checked in `tst_plugins` instead.
* The plugin's `QSettings` uses the tests' QSettings folder: the folder is
  set in QtCore, which the plugin shares.

## Phases

1. **Boundary without loading.** `TrackInfo`, `IPlayerListener`,
   `IPlayerHost` and `PluginPlayerHost`; the lyrics classes move into
   `mylyrics` (Qt and `src/pluginapi/` only).
2. **Loading.** `IPlugin`, `IPluginHost`, `IPanelProvider`,
   `ISettingsPageProvider`, `IPluginSettingsPage`, `PluginManager`,
   `lyricsplugin`; the info panel and `SettingsDialog` use the capabilities;
   lyrics leaves `MainWindow`, `mainwindow.ui` and `SettingsDialog`.
3. **Docs.** architecture.md, info-panel.md, README.md (where plugins go when
   running from a build).

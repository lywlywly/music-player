# The Plugin System

A guide to how plugins work in this app, with the background needed to follow
it. Read it before the code. The detailed design, the reasons behind each
choice and the rejected alternatives are in
[docs/plugins.md](../docs/plugins.md).

## What a plugin is

A **plugin** is a separate library file that the app finds and loads when it
starts: a DLL on Windows, a shared library on macOS and Linux. It adds a
feature that the app does not know about in advance. The app is built without
it and runs without it; with it, the app gains a tab, a Settings page or
anything else the plugin offers.

The **Lyrics** tab is the first, and so far only, plugin. The Properties and
Spectrum tabs are still built into the app.

## Background

### Why a plugin cannot use the app's classes

The app is two parts: `MYLib`, a static library with almost all of the app's
code, and `EXEC`, the executable that links it. A static library is copied
into whatever links it. If a plugin linked `MYLib` too, the process would hold
two copies of the app's code and its global state: the plugin's copy of
`FieldTypePool` would be empty, and an app object passed to the plugin would
not be recognized by the plugin's copy of its class.

So a plugin never uses app classes. It depends on two things only:

* **Qt**, which is a set of shared DLLs: one copy in the process, used by both
  the app and the plugin. Qt types (`QString`, `QWidget`, ...) are safe to pass
  either way.
* **The plugin API** (`src/pluginapi/`): a few header-only interfaces written
  for plugins, using Qt types and plain data only.

The build enforces this. The API's CMake target puts `src/pluginapi/` on a
plugin's include path, but not `src/`, so plugin code that includes an app
header does not compile.

### Talking through interfaces

An **interface** here is a C++ class of virtual methods, mostly pure virtual,
such as:

```cpp
class IPanelProvider {
public:
  virtual QString panelTitle() const = 0;
  virtual QWidget *createPanel(QWidget *parent) = 0;
};
```

One side implements it, the other calls it. A virtual call goes through the
object's own function table, so the caller needs none of the implementer's
code: the app can call a function inside the plugin, and the plugin can call a
function inside the app, knowing only the interface.

### The problem Qt solves: "what is this object?"

When the app loads a plugin, it gets back one object it knows nothing about,
the plugin's **root object**. It must ask that object what it can do: can it
make an info panel tab? A Settings page?

Plain C++ cannot answer reliably here. The app and the plugin are compiled
separately, so each has its own copy of every interface, and `dynamic_cast`
cannot be relied on to treat the two copies as one type. Qt answers with
**names**: each interface gets a string, and both sides compare strings.

## Qt's plugin machinery

Qt's plugin support is in QtCore, gathered in the header `<QtPlugin>`. Five
pieces matter:

| Piece | Where | What it does |
| --- | --- | --- |
| `Q_DECLARE_INTERFACE(IPlugin, "myplayer.IPlugin/1")` | after the interface, in the API header | Gives the interface its name, the **IID** (interface identifier). The part after `/` is its version. |
| `Q_PLUGIN_METADATA(IID "myplayer.IPlugin/1")` | in the root class | Marks the library as a Qt plugin: writes a label into it saying what kind of plugin it is, and adds the entry point Qt calls to create the root object. |
| `Q_INTERFACES(IPlugin IPanelProvider ...)` | in the root class | Lists the interfaces the root object implements: its answer sheet for "can you do this?". |
| `QPluginLoader` | in the app | `metaData()` reads the label without running any plugin code; `instance()` loads the library and creates the root object, returned as a `QObject *`. |
| `qobject_cast<IPanelProvider *>(root)` | in the app | Asks the object for the interface with that IID. It answers from its `Q_INTERFACES` list: an `IPanelProvider *` if it has it, null if not. |

How they work together:

* `Q_PLUGIN_METADATA` and `Q_INTERFACES` expand to nothing for the compiler.
  They are notes for **moc**, Qt's code generator, which writes the label, the
  entry point (with the macros in `qplugin.h`) and the answer sheet into the
  root class's generated code. So the root class needs `Q_OBJECT`, and the
  library is a plugin only once moc has run on it.
* The label also records the Qt version and whether it is a Debug or Release
  build. Qt refuses a plugin built against another major or a newer minor Qt
  version than the app's, and on Windows with MSVC one of the other build
  type. It does so when it reads the label: `metaData()` returns nothing for
  such a plugin, and `instance()` fails.
* `instance()` returns the same root object every time for the same file, for
  the life of the process.

Only the root object needs any of this, because it is the only object the app
receives without knowing its type. Every other object is handed over already
typed: the app passes the plugin its host as an `IPluginHost &`, and the plugin
returns a settings page as an `IPluginSettingsPage *`. Those interfaces have no
IID and are never checked with `qobject_cast`.

## The root object

A plugin's **root object** is the one object Qt creates for the app when it
loads the plugin. Its class is the one marked with `Q_PLUGIN_METADATA`, and a
library can have only one such class: in the lyrics plugin, `LyricsPlugin`.

```cpp
class LyricsPlugin : public QObject, public IPlugin,
                     public IPanelProvider, public ISettingsPageProvider {
  Q_OBJECT
  Q_PLUGIN_METADATA(IID IPlugin_iid)          // this class is the root
  Q_INTERFACES(IPlugin IPanelProvider ISettingsPageProvider)
  ...
};
```

It is the **root** because everything the app gets from the plugin starts
there. The app never creates plugin objects itself: it asks the root object
what it can do, and calls it to create what it needs.

```
LyricsPlugin (root object, created by Qt)
 ├─ createPanel()        → LyricsPanel, with its LyricsPresenter
 └─ createSettingsPage() → LyricsSettingsPage
```

"Root" does not mean the top of a Qt parent-child tree: the root object has
no parent, and the panel and page it creates are owned by the app's widgets
(the info panel's tab widget, the Settings dialog), not by it.

There is **one** in two senses:

* **One per plugin.** Each library gives the app exactly one root object.
* **One per process.** `QPluginLoader::instance()` returns the same object
  every time for the same file, for as long as the app runs. So only one
  `PluginManager` may exist at a time: two would initialize the same object
  twice.

Another common design, the COM style, has no root object: a library registers
many separate service objects, and the host looks them up by interface across
all plugins. Here the app asks each plugin's single root object instead, which
is simpler while each plugin adds only a few things
([docs/plugins.md](../docs/plugins.md), "Rejected").

## The plugin API

The interfaces in `src/pluginapi/` come in three groups.

| Group | Interface | Implemented by | Role |
| --- | --- | --- | --- |
| Core | `IPlugin` | plugin (root object) | `id()`, a stable name such as `lyrics`; `initialize(host)` after loading; `shutdown()` before the host is destroyed |
| Capabilities | `IPanelProvider` | plugin (root object) | Makes a tab for the info panel |
| | `ISettingsPageProvider` | plugin (root object) | Makes a page for the Settings dialog, as an `IPluginSettingsPage` (`widget()`, `apply()` on OK) |
| Host services | `IPluginHost` | app | What the app offers plugins: today only `player()` |
| | `IPlayerHost` | app | `addListener` / `removeListener` for playback events |
| | `IPlayerListener` | plugin | Receives `trackStarted(TrackInfo)`, `stopped()`, `positionChanged(ms)` |
| | `TrackInfo` | (plain data) | The playing song's fields as text, copied from the app's song |

Three ideas run through it:

* **Capabilities are optional.** A plugin implements only the ones it needs.
  Each place in the app that uses a capability asks every plugin for it, and
  skips those without it. Adding a new kind of contribution later means adding
  a new capability, without changing existing ones or existing plugins.
* **The host comes with `initialize`,** not with a capability, so even a
  plugin with no UI at all can follow playback.
* **Events are plain calls.** The app's signals carry app types, which a
  plugin cannot use. The host listens to them and calls each listener's
  virtual method with plain data instead. No signal crosses into the plugin;
  inside it, Qt signals work as usual.

## The pieces in this repository

```
        app (EXEC)                               lyricsplugin.dll
 ┌───────────────────────────┐            ┌───────────────────────────┐
 │ PlaybackController        │            │ LyricsPlugin (root object)│
 │        │ signals          │            │   makes the Lyrics tab    │
 │        ▼                  │            │   and its Settings page   │
 │ PluginPlayerHost ─────────┼── events ──┼─▶ LyricsPresenter         │
 │ PluginManager: loads,     │            │   (the tab's presenter)   │
 │   hosts, shuts down       │            │   gets the events         │
 │ MainWindow, SettingsDialog│            │                           │
 │   add the tab and page    │            │                           │
 └───────────────────────────┘            └───────────────────────────┘
          both include the plugin API headers (src/pluginapi/)
```

| Piece | Where | Role |
| --- | --- | --- |
| `pluginapi` | `src/pluginapi/` | The interfaces, header-only |
| `PluginManager` | app | Loads the plugins, is their `IPluginHost`, shuts them down |
| `PluginPlayerHost` | app | The `IPlayerHost`: turns `PlaybackController`'s signals into listener calls |
| `MainWindow`, `SettingsDialog` | app | Use the capabilities: add the plugins' tabs and Settings pages |
| `mylyrics` | `src/plugins/lyrics/` | The lyrics code: panel, presenter, lyrics loading, settings page |
| `lyricsplugin` | `src/plugins/lyrics/` | The DLL: only the root object, `LyricsPlugin`, linking `mylyrics` |

## How it builds

* `pluginapi` is a CMake `INTERFACE` library: no code, only its include path
  and a link to Qt Core.
* `mylyrics` is a static library linking Qt Widgets and `pluginapi`. The tests
  link it directly, without loading a DLL.
* `lyricsplugin` is a `MODULE` library, CMake's kind for a library that is
  only loaded at runtime, never linked. It holds just the root object: with
  `Q_PLUGIN_METADATA` inside `mylyrics`, every test linking `mylyrics` would
  become a plugin too.
* The build writes the DLL into `plugins/` next to the app (`PlugIns` inside a
  macOS app bundle) and copies it to `Tests/plugins/` for the tests.
* The app never links the plugin, but has a build dependency on it, so
  building the app builds the plugin.

## What happens at runtime

1. **Loading.** `MainWindow` creates a `PluginManager` for the plugins folder.
   For each file, in name order, the manager:
   1. skips it if it is not a library;
   2. reads its label and skips it if it is not a plugin Qt can load (another
      Qt version or build type) or does not have the IID
      `myplayer.IPlugin/1`, so another program's plugin or an old version
      never runs;
   3. loads it and creates the root object (`instance()`), skipping it if
      that fails;
   4. skips it if the root object does not implement `IPlugin`, or if its
      `id()` is already taken;
   5. calls `initialize(host)` and keeps it.

   Every skip except the first is logged.
2. **Using the capabilities.** The info panel asks each plugin for
   `IPanelProvider` and adds a tab for each one, before Properties and
   Spectrum. Each time Preferences opens, the Settings dialog asks for
   `ISettingsPageProvider` and adds a page for each one, and calls each
   page's `apply()` on OK.
3. **Following playback.** The Lyrics tab's presenter adds itself as a
   listener when it is created. When a song starts, playback stops or the
   position moves, `PluginPlayerHost` calls `trackStarted`, `stopped` or
   `positionChanged` on each listener.
4. **Shutdown.** The window's widgets are destroyed first, with the plugins'
   tabs; their presenters remove themselves as listeners. Then the
   `PluginManager` calls `shutdown()` on each plugin. Libraries are never
   unloaded while the app runs: objects they created may still exist, and
   their code must stay in memory.

## Versions and compatibility

* **The core API's version** is `IPlugin`'s IID. It covers every core and host
  interface and `TrackInfo`, which have no IID of their own. Any change to
  them bumps it (`myplayer.IPlugin/2`), and a plugin built for the old version
  is refused at the label check, before any of its code runs, instead of
  calling functions that no longer match.
* **Each capability has its own version.** A plugin built for an older
  `IPanelProvider` still loads, but gets no tab.
* **Same Qt, same build type.** Our rule is stricter than Qt's own check (see
  "Qt's plugin machinery"):
  a plugin is built with the same Qt and the same Debug or Release build as
  the app. The API passes Qt types across, and building the plugins with the
  app keeps this true without effort.

All plugins are built in this repository, with the app, so in practice a
mismatch only happens with an old DLL left in the plugins folder.

## Adding a plugin

1. Create `src/plugins/<name>/` with a `CMakeLists.txt` modeled on the lyrics
   one: a `MODULE` library that links only Qt and `pluginapi`, written into
   the plugins folder.
2. Write a root class deriving from `QObject`, `IPlugin` and the capabilities
   it offers, with `Q_OBJECT`, `Q_PLUGIN_METADATA(IID IPlugin_iid)` and
   `Q_INTERFACES(...)` listing those interfaces.
3. Give it a stable `id()`: it names the plugin's tab in the saved settings,
   and its own settings keys start with it (`<id>/...`).
4. To follow playback, add a listener to `host.player()`, and remove it by the
   time `shutdown()` returns.
5. Register it in `src/CMakeLists.txt` (`add_subdirectory`, and a dependency
   of `EXEC`).

## Where to read next

* [docs/plugins.md](../docs/plugins.md): the full design, the rules for
  plugins, the rejected alternatives and the tests.
* `src/pluginapi/`: the interfaces, each with a short comment.
* `src/pluginmanager.cpp`: the loading steps above, in about fifty lines.
* `src/plugins/lyrics/lyricsplugin.cpp`: a complete root object.

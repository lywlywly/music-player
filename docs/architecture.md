# Architecture and Implementation Notes

The design shared by the whole app: its principles, main classes, startup,
storage and threading. Each feature's design is in its own doc, listed in
[README.md](README.md).

## Overview

`MainWindow` wires the subsystems together. Songs live in `SongLibrary`,
stored in SQLite (`DatabaseManager`) and described by
`ColumnRegistry`. Songs enter the library from manual playlists (Open, Add
Folder) or from watched folders (`MonitoredLibrary`). Playlists (`Playlist`,
`PlaylistCollection`) order songs; autoplaylists are views of the watched
folders' songs that match an expression.
`PlaybackQueue` and `PlaybackManager` decide what plays, `PlaybackController`
runs playback on top of them, and an `AudioPlayer` backend (Qt Multimedia or
GStreamer) plays it. Qt models, widgets and dialogs
(`PlaylistTableModel`, `PlaylistTabs`, ...) only present backend state.

* Startup: load the library (and the plugins' song field values beside it)
  on a worker thread, then build playlists and tabs on the main thread, then
  scan the watched folders in the background ("Startup loading").
* Adding files: parse tags, store the song, append it to the playlist; or a
  scan finds the file in a watched folder ("Song ingest and metadata
  refresh").
* Playback: pick the next song, play it, and follow its position to update
  the status bar, lyrics and system media ([playback.md](playback.md)); the
  Play Stats plugin counts the play once it is mostly listened to
  ([plugins/playstats.md](plugins/playstats.md)).

## Design Principles

### Backend classes do not leak UI

Backend classes hold app state and operations; frontend classes present them.
The backend must not depend on how (or whether) it is displayed.

* Backend: `SongLibrary`, `DatabaseManager`, `Playlist`, `PlaylistCollection`,
  `PlaybackQueue`, `PlaybackManager`, playback policies,
  `PlaybackController`, `MonitoredLibrary`,
  `LibraryScanner`, `IFileSystemMonitor` implementations, `SongMatcher`.
* Frontend (Qt view adapters and widgets): `PlaylistTableModel`,
  `PlaylistTableView`, `PlaylistTabs`, `LibrarySearchResultsModel`, dialogs,
  presenters (`SongInfoPresenter`, `CoverArtPresenter`, ...),
  `SongInfoPanel`, `buildSongInfo`, `PanelLayout`, `PanelLayoutView`,
  `PanelLayoutEditor`, `MainWindowLayout`, `PluginManager`,
  `PluginPlayerHost`, `PluginAudioHost`, `PluginsPage`, `MainWindow`.
* The players' audio tap for plugins (`PcmTap`, filled by `audiotaps`;
  [playback.md](playback.md), "Audio tap") and the play position estimate
  (`PlayPositionEstimate`; [plugins/plugin-system.md](plugins/plugin-system.md),
  "Audio for plugins") use no Qt.
* The layout module (`src/layout/`, library `mylayout`: `PanelLayout`,
  `PanelLayoutView`, `PanelLayoutEditor`) depends on Qt only, never on app
  classes; its tests link it without the app, so using an app class fails
  to link them. The app gives it the elements and their ids, the default
  layouts (`MainWindowLayout`) and the empty-slot text. See
  [layout-editing.md](layout-editing.md).
* A plugin (`src/plugins/<name>/`) depends on Qt and the plugin API
  (`src/pluginapi/`) only. The app never links it: `PluginManager` loads it
  at startup, and the plugin sees the app only through the API's host
  interfaces. See [plugins/plugin-system.md](plugins/plugin-system.md).

| Backend classes may use | Backend classes must not use |
|---|---|
| `QObject`, `Q_OBJECT`, signals, `connect` | `QAbstractItemModel` / `QAbstractTableModel` and model signals |
| QtCore and QtSql types, std types | `QModelIndex`, `QVariant` display data, item roles, `headerData` |
| | column layout (`GlobalColumnLayoutManager`, column titles/widths) |
| | presentation: status glyphs, icons, colors |
| | QtWidgets, QtGui |

Rules that follow from this:

* Backend state changes are reported through backend signals; a frontend
  adapter translates them into its own notifications. For example,
  `Playlist::aboutToInsertSongs` / `songsInserted` become
  `beginInsertRows` / `endInsertRows` in `PlaylistTableModel`.
* Backend signals are emitted synchronously on the calling thread (direct
  connections). Insert, remove and reset use before/after pairs (state is old
  at the "about to" signal and new at the "after" signal); reorder and
  song-data changes are after-only. Handlers may read the sender but must not
  mutate it.
* Data that combines several backend sources for display is joined in the
  frontend. Playback status is owned by `PlaybackQueue` (`statusOf`,
  `statusChanged`); `Playlist` does not know about it, and
  `PlaylistTableModel` joins status with rows.
* Backend classes are tested without any view (`tst_playlist`,
  `tst_playlistcollection`, `tst_playbackqueue`, `tst_playbackmanager`).
* Qt signals are the notification mechanism for now. Because no view types
  cross the boundary, they could later be replaced by a framework-neutral
  event type without changing class responsibilities.

Known exception: `SongLibrary` uses `ColumnRegistry::findColumn(...)` (which
returns a `ColumnDefinition` carrying title/width) to check that a field
exists when importing a song.

### Playlist call chain

Which class callers go through to change playlists and songs, and how changes
reach the views.

```
per-playlist:     caller -> Playlist -> SongLibrary
lifecycle/cross:  caller -> PlaylistCollection -> Playlist -> SongLibrary
view updates:     Playlist / PlaybackQueue / layout signals -> PlaylistTableModel -> PlaylistTableView
```

* Callers (`MainWindow`, `PlaylistTabs`, dialogs) perform playlist operations
  through `Playlist` or `PlaylistCollection`, not by writing `SongLibrary`
  directly.
* Song changes outside playlist operations (Song Properties save, a plugin's
  provided values) write `SongLibrary` directly, then call
  `PlaylistCollection::notifySongChanged(songPk)` (or `notifySongsChanged`)
  so every playlist refreshes that song's rows.

## Main Classes and Responsibilities

Expression classes (`libraryexpression_*`, `LibrarySearchDialog`,
`NowPlayingDisplay`, `displayexpression`) are described in
[expressions.md](expressions.md). Play stats and their cloud sync are the
Play Stats plugin's ([plugins/playstats.md](plugins/playstats.md),
[cloud-sync.md](cloud-sync.md)).

### App composition

The top-level window and what it owns.

* `MainWindow`
  * Wires the app’s major subsystems.
  * Owns settings integration and top-level UI flows, and connects the
    playback actions and buttons to `PlaybackController`.
  * Creates the system media interface (`createSystemMediaInterface(this)`)
    and hands it to `SystemMediaPresenter`.
  * Owns `PlaylistCollection` and then `PlaybackController`, created as child
    `QObject`s right after `ui->setupUi(...)`. Children are destroyed in
    creation order, so the UI (tabs, views, models) is destroyed before the
    playlists and playback state it shows.
  * Starts the library load (`loadLibrary`) during startup and builds the
    playlists, tabs and actions once it calls back.
  * Open and Add Folder call `addFilesFromDialog` and `addFolderFromDialog`
    (playlistdialogs.h) on the current playlist. They and Clear Playlist are
    enabled only while a manual playlist is shown
    (`PlaylistTabs::currentPlaylistChanged`).
  * File > New Autoplaylist and Library Search's Save as Autoplaylist call
    `PlaylistTabs::newAutoPlaylist`.
  * Shows the library scan's status in a permanent status bar label
    ([monitored-library.md](monitored-library.md), "Frontend").
  * Creates `PluginManager`, which lists the plugins and loads those not
    disabled, as a child after `PlaybackController`, so the plugins' panels
    (in the UI) are destroyed before it shuts the plugins down. It passes the
    loaded plugins' panels to the layout, their menus to the Playback menu,
    and the manager's entries to `SettingsDialog`. See
    [plugins/plugin-system.md](plugins/plugin-system.md).
  * The central widget holds two editable layouts, edited from View > Edit
    Layout: the toolbar of playback controls (`layout/toolbar`) and below it
    the panel layout (`layout/tree`), splitters and tabs of the panels.
    `setUpLayout` registers each layout's elements and builds them
    ([layout-editing.md](layout-editing.md)).
  * Connects the presenters to their widgets and to `PlaybackController`:
    the Properties panel and the cover ([info-panel.md](info-panel.md)).
  * Edit > Search Playlist shows the one `PlaylistSearchDialog`
    ([monitored-library.md](monitored-library.md), "Playlist Search"). The
    destructor deletes it first, as it uses `columnLayoutManager_`, a member
    destroyed before the window's children.

### Persistence and schema

The SQLite database every other class reads and writes through.

* `DatabaseManager`
  * Owns SQLite connection, pragmas, and schema setup.
  * Creates tables for songs, playlists (`playlists`, `playlist_items`,
    `autoplaylist_order`), dynamic/computed attributes, and the monitored
    library (`library_folders`, `monitored_files`). Play stats are in the
    Play Stats plugin's own database
    ([plugins/playstats.md](plugins/playstats.md)).

### Column and layout metadata

Which fields a song can have (built-in, custom tag, computed), their types
and display formats, and how columns are laid out.

* `ColumnRegistry`
  * Source of truth for built-in, dynamic, and computed column definitions.
  * Owns both `ColumnDefinition` (UI-facing column metadata) and
    `FieldDefinition` (value type/display/searchability metadata).
  * Exports parser symbols (`ExprSymbolInfo`) for expression field resolution.
* `GlobalColumnLayoutManager`
  * Stores visible order/width/visibility for columns across playlist/search views.
* `FieldTypePool`
  * Process-wide pool of `FieldDefinition`, keyed by `fieldId`.
  * `FieldValue` resolves type/display behavior through this pool.
  * `ColumnRegistry` and `StatusRuntimeSymbolTable` upsert active definitions.
* The Settings dialog's Fields page lists, adds and removes custom tag
  fields (display name, tag key, value type, visible by default) and
  computed fields (the same, with an expression whose type must match the
  value type). OK writes the changes through `ColumnRegistry` in one
  database transaction and, when the set of fields changed, emits
  `customFieldsChanged`.

### Song domain

The in-memory song store and reading/writing audio file tags.

* `SongLibrary`
  * Canonical song repository in memory + DB sync.
  * Loads built-in, dynamic and computed fields.
  * Parses/imports files, refreshes metadata, evaluates computed fields, and
    evaluates library search expressions.
  * Keeps a copy of the fields plugins provide (play stats): per source, a
    key per song from its tag fields, songs grouped by key, and the keys to
    read again, refreshed in one batch from the event loop (`markStale`,
    `refreshStale`). See [plugins/playstats.md](plugins/playstats.md),
    "What `SongLibrary` keeps".
  * `addTolibrary` stores the canonical file path
    (`QFileInfo::canonicalFilePath`, or the cleaned absolute path for a
    missing file), so one file never gets two rows.
  * `removeSongs(songPks)` deletes songs from `songs` (attributes and
    `playlist_items` cascade) and from memory, including registered queries.
    A re-added file gets a new pk (`song_id` is `AUTOINCREMENT`, never
    reused); a plugin's values are by key, so it gets them back.
  * `takeSnapshot()` moves the worker-loaded library state (songs, paths,
    provided keys) out as a plain-data `Snapshot` for the main-thread
    instance, without copying it.
* `MonitoredLibrary`, `LibraryScanner`, `EfswFileSystemMonitor` (backend,
  owned by `MainWindow`) and `LibraryFoldersPage` (the Settings > Library
  page): watched folders and the songs under them, kept in sync by scans
  that the file system watcher triggers. See
  [monitored-library.md](monitored-library.md).
* `SongParser`
  * Parses file tags into built-in + dynamic fields and remaining raw tag fields.
  * Multi-value tag fields are displayed as comma-separated text (`", "`).
  * Writes updated/removed tags back to audio files (`writeTags`).
  * Uses TagLib built from the bundled fork submodule
    ([`third_party/taglib`](https://github.com/lywlywly/taglib)) by default, so TagLib source edits
    in that submodule are applied after rebuild.
  * ALAC bitrate prefers average bitrate derived from MP4 `mdat` payload bytes
    and duration.

### Playlist domain

`Playlist` and `PlaylistCollection` are backend classes (see "Backend classes
do not leak UI"); `PlaylistTableModel`, `PlaylistTableView` and `PlaylistTabs`
are the Qt frontend.

* `PlaylistCollection` (backend, owned by `MainWindow`)
  * Owns all `Playlist`s (`std::map<int, std::unique_ptr<Playlist>>`; each is
    created and loaded before insertion, and its address stays stable).
  * Owns playlist metadata behavior (`playlists` table): load, create/delete playlist rows, rename, persist/repair `tab_order`. Refuses to remove the last playlist.
  * `loadAll()` emits no signals; after it, `playlistCreated` / `playlistAboutToBeRemoved` / `playlistRemoved` / `playlistRenamed` report changes. `playlistAboutToBeRemoved` is sent while the `Playlist` still exists.
  * `notifySongChanged(songPk)` / `notifySongsChanged(songPks)` update autoplaylist rows and refresh a song's rows in every playlist after non-playlist song writes (a plugin's provided values, Song Properties, `Playlist::songsRefreshed`, and the songs `MonitoredLibrary` added or re-read: `songsEntered`, `songsUpdated`).
  * Fills and updates the autoplaylists, and owns song deletion
    (`releaseSongs`, the only place that deletes songs: those in no manual
    playlist and not monitored). See
    [monitored-library.md](monitored-library.md), "Invariants" and
    "Autoplaylists".
* `PlaylistTabs` (frontend)
  * Builds tabs from a loaded `PlaylistCollection` and follows its signals; delegates create/remove/rename/reorder to it.
  * Creates one `PlaylistTableView` per tab and handles its row context menu and double-click.
  * Properties (row context menu, and the table's shortcut) and Open Containing Folder (row context menu) call `openSongProperties` and `revealSongFile` for the row's song.
  * Remove from Playlist (row context menu) removes the selected rows with `Playlist::removeSongs`.
  * Clear Playlist (row context menu, and the `MainWindow` menu action) calls `clearCurrentPlaylist`.
  * Autoplaylist tabs and menus, and `newAutoPlaylist` / `editAutoPlaylist`
    (`AutoPlaylistDialog`): [monitored-library.md](monitored-library.md),
    "Frontend".
  * Emits `currentPlaylistChanged` when the shown playlist changes, and
    `selectedSongChanged(playlist, songPk)` for the first selected row
    ([info-panel.md](info-panel.md), "`PlaylistTabs`").
  * Refresh (tab context menu) re-reads the playlist's tags with `refreshMetadataWithProgress`.
  * The Properties context-menu action carries `PlaylistTableView::propertiesShortcuts()`
    only so the menu can display the platform binding (`Command+I` on macOS,
    `Alt+Enter` elsewhere); the table handles the keys (see `PlaylistTableView`).
* `Playlist` (backend)
  * One playlist: song-id ordering, sorting, `last_played`, and per-playlist operations (`addFiles`, `addSong`, `addSongByPk`, `removeSongs`, `clear`, `sortByField`, `moveRows`, `refreshMetadataFromFiles`).
  * `Kind::Manual` or `Kind::Auto`. An auto playlist refuses the write operations, and `PlaylistCollection` sets its rows ([monitored-library.md](monitored-library.md), "Classes").
  * Both kinds can be reordered: `sortByField` and `moveRows(rows, destinationRow)` (a drag) renumber the stored positions to the new rows, so the order survives a restart.
  * A song appears at most once per playlist. Adding a song already present is a silent no-op (`addSong` / `addSongByPk` return `false`; `addFiles` skips it and returns only the appended pks). `playlist_items` enforces this with `UNIQUE (playlist_id, song_id)`.
  * Ingests/refreshes songs through `SongLibrary`; owns `playlist_items`, `autoplaylist_order` and `playlists.last_played` persistence. `playlistId <= 0` is non-persistent (used by tests).
  * Sends before/after signals for insert, remove, reset and reorder (`aboutToReorderSongs` / `songsReordered`), and after-only signals for song-data changes. `addFiles` parses and ingests all paths first, then sends one insert pair for the batch. `removeSongs` sends one remove pair per row; after it and `clear`, `songPksRemoved(songPks)` reports the songs that left. Does not know playback status.
* `SongMatcher`, `matchPlaylistRows` (backend, playlistsearch.h) and
  `PlaylistSearchDialog` (frontend): Playlist Search
  ([monitored-library.md](monitored-library.md), "Playlist Search").
* `PlaylistTableModel` (frontend)
  * `QAbstractTableModel` adapter for one `Playlist`: columns, header, cell text, sortable-column check (`sortByColumnId`).
  * Renders the status column from `PlaybackQueue::statusOf` and refreshes only the status cell on `PlaybackQueue::statusChanged` for its own playlist.
  * Holds the `Playlist` through a `QPointer`, so it reports no rows if the playlist is destroyed first.
  * A reorder is a layout change (`layoutAboutToBeChanged` / `layoutChanged`): persistent indexes are moved by song pk, so the view's selection and current row follow their songs (and so does "Playback follows cursor").
  * Drag and drop: rows are draggable and drops go between rows. `mimeData` encodes the dragged songs' pks as `PlaylistTableModel::kRowsMimeType` with the source playlist (pks, not rows: `QDrag::exec` runs an event loop, and an autoplaylist's rows can change before the drop); `dropMimeData` accepts only a move from the same playlist, looks up the songs' current rows and calls `Playlist::moveRows`, and returns whether the order changed. The model has no `removeRows`, so the view's follow-up removal after a move does nothing.
* `PlaylistTableView` (frontend)
  * `QTableView` of one `Playlist` through its own `PlaylistTableModel`.
  * Header: applies `GlobalColumnLayoutManager` widths on `layoutChanged`, and writes back column moves, resizes and the header menu's visibility changes. Clicking a sortable column sorts the playlist (`sortByColumnId`).
  * Rows can be dragged to reorder the playlist (`InternalMove`, both kinds); a drop that moves rows hides the sort indicator.
  * Handles the Properties keys with a `QShortcut` per key and emits `propertiesRequested(row)` for the first selected row, or the current row when none is selected. Qt does not reliably dispatch `QAction` shortcuts from a transient popup `QMenu`, and the keys must act on the selection rather than the right-clicked row stored on the context-menu action.

### Song Properties UI

The UI for one song: viewing and editing its tags, and revealing its file.

* `songdialogs.h`
  * `openSongProperties` refreshes the song from its file, opens `SongPropertiesDialog`, and calls `PlaylistCollection::notifySongChanged` after a save.
  * `revealSongFile` shows the file in the file manager, with a warning when the file is missing or cannot be shown.
* `SongPropertiesDialog`
  * Shows refreshed parsed/computed fields first, then remaining raw tag fields.
  * Buffers edits in-memory and writes once on `Save`.
  * If there are no pending changes, `Save` just closes the dialog.
  * Supports edit/add/remove for writable tag rows.
  * Tag value editing support multi-value input using `;` as separator.
  * Computed fields are read-only and non-removable.
* `FieldEditDialog`
  * Secondary editor for a row value (multiline text).
* `AddFieldDialog`
  * Adds a new raw tag key/value pair (no `attr:` prefix input).

### Playback, system media, info panel and other UI

Each has its own doc; the classes are listed here with where they sit.

* Playback ([playback.md](playback.md)): `PlaybackQueue`, `PlaybackManager`,
  the playback policies and `PlaybackController` (backend);
  `PlaybackBackendManager` and the `AudioPlayer` backends
  (`QtAudioPlayer`, `GstAudioPlayer`), and ReplayGain (`ReplayGainTags`).
  The same doc covers stopping and skipping unplayable songs, seeking, the
  OS media controls (`ISystemMediaInterface`,
  `SystemMediaPresenter`) and the GStreamer plugin registry and scanner
  setup on Windows.
* Info panels ([info-panel.md](info-panel.md)): `SongInfoPresenter`,
  `SongInfoPanel` and `buildSongInfo` (the Properties panel) and
  `CoverArtPresenter` (frontend). The Spectrum panel (`SpectrumPresenter`,
  `SpectrumWidget`, the analysis, in `src/plugins/spectrum/`) is in
  [plugins/spectrum.md](plugins/spectrum.md); the Lyrics panel
  (`LyricsPanel`, `LyricsPresenter`, `LyricsLoader`, `LyricsManager`,
  `LyricsSettingsPage`, in `src/plugins/lyrics/`) in
  [plugins/lyrics.md](plugins/lyrics.md), and the A-B Repeat plugin
  (`ABRepeat`, in `src/plugins/abrepeat/`) in
  [plugins/abrepeat.md](plugins/abrepeat.md). The Play Stats plugin
  (`PlayStats`, `PlayStatsStore`, the cloud sync classes, in
  `src/plugins/playstats/`) is in [plugins/playstats.md](plugins/playstats.md).
* `SystemMediaPresenter`, `CoverArtPresenter` and `NowPlayingPresenter` do not
  know about each other; `MainWindow` drives them from `PlaybackController`'s
  signals.
* The app icon, toolbar icons, the Windows font and table selection, and the
  theme:
  [ui-styling.md](ui-styling.md).
* Event ordering: re-entry in synchronous signals and listener calls,
  races between commands and playback events, and the plan for a
  synchronous core with queued edges: [event-ordering.md](event-ordering.md).
* The Settings dialog (`SettingsDialog`), its navigation tree, its pages and
  the docs that describe each: [settings-dialog.md](settings-dialog.md). The
  Plugins page (`PluginsPage`) is in
  [plugins/plugin-system.md](plugins/plugin-system.md).

## Core Feature Implementation Notes

### Startup loading

How the library loads off the main thread, and what stays disabled until it
is ready.

* File-backed startup keeps library-dependent actions disabled until playlist
  hydration finishes and shows `Loading library...` in the status bar.
* `loadLibrary` (libraryloader.h) loads the custom columns, the songs and the
  values of the plugins' song fields. For a file-backed database, a worker
  thread creates its own `ColumnRegistry`, `DatabaseManager`, and
  `SongLibrary` with a separate SQLite connection, computes each song's keys,
  fills the values each provider's `allValues` read in a task of its own
  beside the songs' load, and returns a `SongLibrary::Snapshot` (moved, not
  copied); on the main thread `loadLibrary` then loads the custom columns
  into the app's `ColumnRegistry`, applies the snapshot to the app-owned
  `SongLibrary` (which refreshes the keys marked stale during the load), and
  calls back. The watcher for the worker is a child of `MainWindow`, so
  destroying the window during the load drops the result; `~MainWindow`
  waits for the worker (the future `loadLibrary` returns), since it calls
  the plugins' providers, before the plugins shut down
  ([plugins/playstats.md](plugins/playstats.md), "Startup", "Lifetime").
* In the callback, `MainWindow` loads and watches the monitored library
  (`MonitoredLibrary::load()`) and loads playlists (`PlaylistCollection::loadAll()`),
  creates playlist tabs/models, re-enables actions, restores the normal
  status-bar message (through `NowPlayingPresenter::reloadExpressions()`),
  and starts the monitored library scan in the background
  (`MonitoredLibrary::rescan()`).
* Custom (`attr:`) columns enter `ColumnRegistry` through
  `ColumnRegistry::loadDynamicColumns` in `loadLibrary`, but the display
  expressions are parsed earlier, when the `MainWindow` constructor creates
  `NowPlayingPresenter`.
  Field names are resolved at parse time, so `finishSetUpPlaylist` re-parses
  them (`NowPlayingPresenter::reloadExpressions()`); without that, a custom field
  would show its name inside `${}` (`[mood]` instead of `[happy]`), or the
  expression would fall back to the default, until Settings is applied.
  See [expressions.md](expressions.md) for how display expressions resolve
  fields.
* Startup-sensitive handlers are guarded in addition to disabling actions, so
  direct/system-media requests cannot play, seek or load songs before playlist
  hydration finishes.
* `:memory:` databases keep the synchronous path for tests because separate
  SQLite in-memory connections do not share state: `loadLibrary` loads on the
  calling thread and calls back before it returns.

### Song ingest and metadata refresh

How files become songs, and how songs are refreshed after their tags change.

* Ingest path: parse file -> evaluate computed fields -> upsert song -> sync dynamic/computed attributes -> file the song under each provided source's key, whose values are read at the next refresh.
  Adding files to a playlist (open, add folder) goes through
  `Playlist::addFiles(...)`, which runs this path for each file and then
  appends the rows. A monitored library scan parses on a worker thread
  (`LibraryScanner`); `MonitoredLibrary` then runs the rest of the path on
  the main thread (`SongLibrary::prepareParsedSong`, `addTolibrary`).
* `SongParser::parse` returns `std::nullopt` when TagLib cannot open the file
  (missing, unreadable, or unsupported); a readable file without tags is not
  a failure. `Playlist::addFiles(...)` skips such paths and reports them
  through its optional `failedPaths` output. `addFilesFromDialog` shows an
  error dialog only when the single selected file failed; multi-file and
  folder adds skip failed files silently.
* Refresh path re-parses file and syncs built-in/dynamic/computed fields to DB + memory.
  If parsing fails, the stored fields are kept unchanged.
* Computed values are namespaced in memory (`computed:<key>`) but persisted in
  DB with plain keys (`<key>`) in `song_computed_attributes`.
* Properties save path:
  * collect dirty/removed rows -> `SongParser::writeTags(...)` -> `SongLibrary::refreshSongFromFile(...)` -> emit song-updated signal -> `MonitoredLibrary::noteSongWritten(...)` stores the file's new stats (so the next scan does not re-parse it) and `PlaylistCollection::notifySongChanged(...)` refreshes the song's rows.
* Two bulk user-triggered flows are intentionally blocking with progress UI:
  * `refreshMetadataWithProgress(...)` (playlist-wide metadata refresh
    via `Playlist::refreshMetadataFromFiles(...)`)
  * `addFolderFromDialog(...)` (folder import via `Playlist::addFiles(...)`)
  Both are in `playlistdialogs.h` and share one modal progress dialog that
  processes events while work runs, instead of moving those operations to
  background threads.

### FieldValue and typed conversion behavior

How one field value keeps its text, typed value and display format.

* `FieldValue` stores:
  * canonical `text`
  * `fieldId` (schema key)
  * typed union (`numberDouble` / `numberInt` / `boolean`)
* Type/display metadata is resolved from `fieldId` through `FieldTypePool`
  during `FieldValue::assign(...)`.
* `assign(...)` parses by declared `ValueType` from `FieldTypePool`.
  * On parse success, typed union stores parsed value.
  * On parse failure, typed union keeps type default:
    * Number -> `0.0`
    * DateTime -> `0`
    * Boolean -> `false`
* Typed conversion helpers are centralized:
  * `parseNumber`
  * `parseBoolean`
  * `parseDateTimeEpochMs`
* DateTime parse accepts:
  * year/month/day variants
  * ISO date/datetime
  * plain `yyyy-MM-dd HH:mm:ss`
  * numeric epoch values (seconds or milliseconds)
* Non-text typed handling:
  * sorting compares typed union values directly.
  * expression evaluation reads typed union for number/boolean; datetime
    comparisons use shared datetime conversion helpers.
* Display formatting is schema-driven by `FieldDefinition.displayKind` through
  `FieldTypePool`.

### Playlist persistence

What is saved per playlist, and which class writes each table.

* Playlist identity is `playlist_id`.
* Playlist order is persisted by `tab_order`.
* Last opened playlist tab is persisted in `QSettings` (`playlist/last_opened_id`).
* Per-playlist resume pointer is persisted by `last_played`. Play with nothing
  playing resumes it, or starts at the first row when that song is not in the
  playlist (new playlists start with `last_played = 1`, or the song was
  removed); an empty playlist does nothing.
* Manual playlist membership and order are persisted in
  `playlist_items(position)`. Autoplaylists store `playlists.kind = 'auto'`
  and `expression`; their rows are computed at load and kept current in
  memory, and their order is persisted in `autoplaylist_order(position)`,
  one row per current row. It is not membership: a song leaving the
  autoplaylist loses its row, and one that starts matching is appended.
* Playlist tab name edits are done inline on the tab bar and persisted to
  `playlists.name`.
* Writers: `PlaylistCollection` owns `playlists` rows (insert, delete, `name`,
  `tab_order`); `Playlist` owns `playlist_items`, `autoplaylist_order` and
  `playlists.last_played`.
* Playlist metadata writes (`playlists` row create/update/delete) stay synchronous.
* Removing a row from a playlist also deletes its `playlist_items` row; a
  song left in no playlist and not in the monitored library is also deleted
  from `songs` (see `PlaylistCollection`).
* A sort or a drag renumbers the playlist's stored positions to 1..n in one
  transaction. New rows get a position above every stored one, so positions
  may have gaps but never collide.

## Threading / Async Model

* Qt UI/model code runs on the main thread, and so do backend playlist and
  playback operations; their signals use direct connections, except
  GStreamer's bus messages on macOS and Windows (below).
* Cloud sync (the Play Stats plugin) runs its HTTP functions synchronously
  on a coordinator-owned worker thread; results are delivered back to the
  main thread, serialized through the worker thread's event queue. See
  [cloud-sync.md](cloud-sync.md).
* The startup load's worker calls the plugins' song field providers
  (`songKey`, `allValues`), which read with SQLite connections of their own
  ([plugins/playstats.md](plugins/playstats.md), "Threading contract").
* Monitored library scans walk and parse files on a `QtConcurrent` worker
  with copies of their input; `MonitoredLibrary` applies the result on the
  main thread in batches, one transaction each. One scan runs at a time.
* efsw runs its own watcher thread; `EfswFileSystemMonitor` only queues its
  events to the main thread.
* GStreamer's bus messages reach the main thread in two hops on macOS and
  Windows: from a streaming thread to a dedicated GLib loop `std::thread`,
  which runs `GstAudioPlayer::onBusMessage`, then queued to the main thread.
  On Linux Qt's event loop is GLib's, so they arrive on the main thread
  directly. See [playback.md](playback.md), "GStreamer bus messages".
* What the thread of each delivery means for the order receivers run in
  (queued bus messages on macOS and Windows, direct calls on Linux), and the
  races that follow: [event-ordering.md](event-ordering.md).
* Playlist DB writes, including the bulk `playlist_items` delete in
  `Playlist::clear()`, are synchronous, so rows appended right after a clear
  are never removed by a late delete. Batches of row writes (appends,
  removals, reorders) run in one transaction each; no caller holds one then
  (SQLite transactions do not nest).

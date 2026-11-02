# Architecture and Implementation Notes

This file contains implementation-level details that are intentionally kept out
of `README.md`.

Two topics have their own files:

* [expressions.md](expressions.md): the expression language used by library
  search, computed fields, and the status bar and window title.
* [cloud-sync.md](cloud-sync.md): optional play-count sync through AWS Lambda
  and DynamoDB.

## Overview

`MainWindow` wires the subsystems together. Songs and play stats live in
`SongLibrary`, stored in SQLite (`DatabaseManager`) and described by
`ColumnRegistry`. Playlists (`Playlist`, `PlaylistCollection`) order songs,
`PlaybackQueue` and `PlaybackManager` decide what plays, `PlaybackController`
runs playback on top of them, and an `AudioPlayer` backend (Qt Multimedia or
GStreamer) plays it. Qt models, widgets and dialogs
(`PlaylistTableModel`, `PlaylistTabs`, ...) only present backend state.

* Startup: load the library on a worker thread, then build playlists and tabs
  on the main thread, then start cloud sync ("Startup loading").
* Adding files: parse tags, store the song, append it to the playlist
  ("Song ingest and metadata refresh").
* Playback: pick the next song, play it, follow its position to update the
  status bar, lyrics and system media, and count the play once it is mostly
  listened to ("Playback domain", "Play statistics").

## Design Principles

### Backend classes do not leak UI

Backend classes hold app state and operations; frontend classes present them.
The backend must not depend on how (or whether) it is displayed.

* Backend: `SongLibrary`, `DatabaseManager`, `Playlist`, `PlaylistCollection`,
  `PlaybackQueue`, `PlaybackManager`, playback policies,
  `PlayCompletionTracker`, `PlaybackController`.
* Frontend (Qt view adapters and widgets): `PlaylistTableModel`,
  `PlaylistTabs`, `LibrarySearchResultsModel`, dialogs, `MainWindow`.

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
view updates:     Playlist / PlaybackQueue / layout signals -> PlaylistTableModel -> QTableView
```

* Callers (`MainWindow`, `PlaylistTabs`, dialogs) perform playlist operations
  through `Playlist` or `PlaylistCollection`, not by writing `SongLibrary`
  directly.
* Song changes outside playlist operations (Song Properties save, play stats,
  cloud sync) write `SongLibrary` directly, then call
  `PlaylistCollection::notifySongChanged(songPk)` so every playlist refreshes
  that song's rows.

## Main Classes and Responsibilities

Expression classes (`libraryexpression_*`, `LibrarySearchDialog`,
`NowPlayingDisplay`, `displayexpression`) are described in
[expressions.md](expressions.md), and the cloud sync classes in
[cloud-sync.md](cloud-sync.md).

### App composition

The top-level window and what it owns.

* `MainWindow`
  * Wires the app’s major subsystems.
  * Owns settings integration and top-level UI flows, and connects the
    playback actions, buttons and system media requests to
    `PlaybackController`.
  * Owns `PlaylistCollection` and then `PlaybackController`, created as child
    `QObject`s right after `ui->setupUi(...)`. Children are destroyed in
    creation order, so the UI (tabs, views, models) is destroyed before the
    playlists and playback state it shows.
  * Starts file-backed library loading on a worker thread during startup and
    hydrates UI state after the library snapshot is ready.

### Persistence and schema

The SQLite database every other class reads and writes through.

* `DatabaseManager`
  * Owns SQLite connection, pragmas, and schema setup.
  * Creates tables for songs, playlists, dynamic/computed attributes, and play stats.

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

### Song domain

The in-memory song store and reading/writing audio file tags.

* `SongLibrary`
  * Canonical song repository in memory + DB sync.
  * Loads built-in, dynamic, computed, and play-stats fields.
  * Parses/imports files, refreshes metadata, evaluates computed fields, and
    evaluates library search expressions.
  * Maintains identity-based play stats using normalized `title|artist|album`.
  * Exposes a plain-data `Snapshot` used to transfer worker-loaded library
    state back to the main-thread instance.
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
do not leak UI"); `PlaylistTableModel` and `PlaylistTabs` are the Qt frontend.

* `PlaylistCollection` (backend, owned by `MainWindow`)
  * Owns all `Playlist`s (`std::map<int, std::unique_ptr<Playlist>>`; each is
    created and loaded before insertion, and its address stays stable).
  * Owns playlist metadata behavior (`playlists` table): load, create/delete playlist rows, rename, persist/repair `tab_order`. Refuses to remove the last playlist.
  * `loadAll()` emits no signals; after it, `playlistCreated` / `playlistAboutToBeRemoved` / `playlistRemoved` / `playlistRenamed` report changes. `playlistAboutToBeRemoved` is sent while the `Playlist` still exists.
  * `notifySongChanged(songPk)` refreshes a song's rows in every playlist after non-playlist song writes (play stats, cloud sync, Song Properties).
* `PlaylistTabs` (frontend)
  * Builds tabs from a loaded `PlaylistCollection` and follows its signals; delegates create/remove/rename/reorder to it.
  * Creates one `PlaylistTableModel` per tab (parented to the tab's table view) and connects UI interactions.
  * Opens per-song Properties dialog from row context menu and refreshes playlist rows after successful save.
  * The Properties context-menu action carries shortcut metadata so the menu can
    display the platform binding (`Command+I` on macOS, `Alt+Enter` elsewhere),
    but the actual keyboard handling is registered with `QShortcut` on each
    playlist table. Qt does not reliably dispatch arbitrary `QAction`
    shortcuts from a transient popup `QMenu`, and keyboard activation must use
    the selected/current row rather than the right-click row stored on the
    context-menu action.
* `Playlist` (backend)
  * One playlist: song-id ordering, sorting, `last_played`, and per-playlist operations (`addFiles`, `addSong`, `addSongByPk`, `removeSong`, `clear`, `sortByField`, `refreshMetadataFromFiles`).
  * A song appears at most once per playlist. Adding a song already present is a silent no-op (`addSong` / `addSongByPk` return `false`; `addFiles` skips it and returns only the appended pks). `playlist_items` enforces this with `UNIQUE (playlist_id, song_id)`.
  * Ingests/refreshes songs through `SongLibrary`; owns `playlist_items` and `playlists.last_played` persistence. `playlistId <= 0` is non-persistent (used by tests).
  * Sends before/after signals for insert, remove and reset, and after-only signals for reorder and song-data changes. `addFiles` parses and ingests all paths first, then sends one insert pair for the batch. Does not know playback status.
* `PlaylistTableModel` (frontend)
  * `QAbstractTableModel` adapter for one `Playlist`: columns, header, cell text, sortable-column check (`sortByColumnId`).
  * Renders the status column from `PlaybackQueue::statusOf` and refreshes only the status cell on `PlaybackQueue::statusChanged` for its own playlist.
  * Holds the `Playlist` through a `QPointer`, so it reports no rows if the playlist is destroyed first.

### Song Properties UI

The dialogs for viewing and editing one song's tags.

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

### Playback domain

Who owns the current song and the queue, how Next/Prev pick a song, how
unplayable songs are skipped, and the audio backends.

* `PlaybackQueue` (backend)
  * Current song pointer + explicit queued songs + playback status.
  * Owns per-song status: `statusOf(songPk, playlist)` returns Playing/Paused/None
    for the current song (queue position not consulted), otherwise Queued with
    its position for a song queued from the same playlist. `statusChanged(songPk,
    playlist)` reports changes.
  * Stores `Playlist` pointers but only compares them, never dereferences
    them. `removePlaylist(playlist)` drops that playlist's current song and
    queued entries; `PlaybackController` calls it on
    `PlaylistCollection::playlistAboutToBeRemoved`, while the id still resolves
    to the `Playlist`.
* `PlaybackManager` (backend)
  * Playback policy orchestration (`next/prev/play/pause/stop`) on top of `PlaybackQueue` + active `Playlist`.
  * Requires `setView(Playlist&)` with a valid playlist before playback/policy operations.
  * Songs removed from a playlist leave stale ids behind: `next()` skips queued
    songs no longer in their playlist, and the policies treat a current song no
    longer in the playlist as no current song (sequential Next starts at the
    first row, Prev at the last).
* `PlaybackController` (backend)
  * Runs playback: the transport commands (play, pause, toggle, stop, next,
    prev, random, play a row, seek), starting a song on the backend, skipping
    unplayable songs and counting completed plays. Owns `PlaybackQueue`,
    `PlaybackManager` and `PlaybackBackendManager`. The first `setBackend`
    creates the backend; `MainWindow` calls it after starting the library
    load, so creating the backend (about 250 ms for Qt Multimedia on a cold
    start) overlaps the load on the worker thread.
  * Knows nothing about widgets. `MainWindow` gives it the shown playlist and
    the `Playback follows cursor` row as callbacks, and shows what its signals
    report: `songAboutToStart` (before the source is set, since the backend
    may report duration and position during `setSource`), `songStarted`,
    `playbackStateChanged` (resume, pause, stop), `movedToRow` (Next, Prev,
    Random), `playbackFailed`, `playCounted`, and the backend's position,
    duration and bitrate. The backend's signals reach `MainWindow` only
    through it, so a backend switch (`setBackend`) needs no reconnecting in
    the window. A backend switch stops playback like `stop()`.
  * Transport commands are ignored until `setReady(true)`, which
    `MainWindow` calls once the library and playlists are loaded, and until
    the backend exists.
* Playback stop and failure handling (`PlaybackController`)
  * Next with nothing to play (the shown playlist is empty) stops playback.
  * Removing the playlist that is playing stops playback before
    `PlaybackQueue::removePlaylist` drops its current song.
  * A song that cannot be played is skipped with `next()`: its file is
    missing (checked in `playSong` before the play stats are updated), or the
    backend reports `InvalidMedia` (`GstAudioPlayer` reports bus errors that
    way too). The previous song's audio stops at once, so its position
    updates cannot reset the failure count and its `EndOfMedia` cannot start
    a second skip. The skip behaves like the user pressing Next: queued songs
    first, then `Playback follows cursor`, then the playback order, and with
    `Cursor follows playback` on the selection moves to the song that plays.
    It runs from a zero-delay timer, not recursively, so the `next()` that hit
    the failure finishes (including its `movedToRow`) before the skip
    starts; `stop()` cancels a pending skip.
  * After as many consecutive failures as the shown playlist has songs,
    playback stops and `playbackFailed(path)` is emitted; `MainWindow` shows
    `Cannot play file: <path>` in the status bar. The count resets when
    playback position advances or on stop.
* Playback policies (`PlaybackPolicySequential`, `PlaybackPolicyShuffle`)
  * `PlaybackPolicyShuffle` sizes its history to half the playlist and follows
    size changes through `Playlist` insert/remove/reset signals; it
    disconnects when switched to another playlist or destroyed.
  * Shuffle steps through history entries still in the playlist; otherwise it
    picks a random row whose song is not in the history. Songs are unique per
    playlist and the history holds at most half the rows, so a candidate
    always exists.
  * `Playback -> Playback follows cursor` passes the first selected row from
    `MainWindow` (through `PlaybackController`) into
    `PlaybackManager::next(...)`: queued songs still take
    priority, then the selected row may be played once if it is not already
    current.
  * `Playback -> Cursor follows playback` gates whether `MainWindow` answers
    `PlaybackController::movedToRow` by calling
    `PlaylistTabs::navigateIndex(...)` to select and scroll to the new row.
  * The toolbar random button is a one-shot random jump within the current
    playlist: it picks a random row directly, calls `playIndex(...)`, and does
    not use shuffle policy history or guarantee non-duplicate selections.
* `PlaybackBackendManager`
  * Runtime backend owner/switcher (`QMediaPlayer` or `GStreamer`).
  * Manages GLib loop thread for GStreamer on macOS/Windows.
  * `MainWindow` prewarms GStreamer startup when GStreamer is the saved backend:
    `gst_init` on all platforms and default output device lookup on macOS. The
    macOS output-device result is startup-only; later output refreshes query
    CoreAudio directly.
* `AudioPlayer` (+ concrete backends)
  * Transport and volume control consumed by `PlaybackController`, which
    re-applies the volume after a backend switch.
  * Exposes `bitrateChanged(bitsPerSecond)` for playback-time bitrate updates.
  * `AudioPlayer` stores clamped volume as an integer percentage (`0..100`);
    `QtAudioPlayer` maps it to `QAudioOutput::setVolume(...)`, and
    `GstAudioPlayer` maps it to the GStreamer `playbin` `volume` property.

### System media

OS media controls and now-playing metadata.

* `ISystemMediaInterface` (+ platform impls)
  * Bridge to OS media controls/metadata.
  * The interface has no QtWidgets dependency. Platform selection lives in
    `createSystemMediaInterface(window)` (`systemmediainterfacefactory.h`),
    which takes the window because Windows needs its native handle; test
    builds return `DummySystemMediaInterface` when
    `MYPLAYER_USE_DUMMY_MEDIA_INTERFACE` is set.

### Lyrics and cover art

Where lyrics and cover images come from, and the lyrics panel settings.

* `LyricsPresenter` (frontend)
  * Shows the playing song's lyrics in `LyricsPanel`, owns the
    `LyricsManager`, and applies the lyrics panel style settings.
  * `MainWindow` calls `showSong(...)` on `PlaybackController::songStarted`,
    `clear()` on stop, and `setPositionMs(...)` on
    `PlaybackController::positionChanged`. The position comes from the
    controller, not the player, because a backend switch replaces the player;
    a direct connection would stay on the old one.
* `CoverArtPresenter` (frontend)
  * Shows the playing song's embedded cover in the cover label, scaled to the
    label on resize and splitter moves.
  * `MainWindow` calls `showSong(...)` on `PlaybackController::songStarted`
    (after `setTitleAndArtist`, which clears the system artwork) and
    `clear()` on stop.
  * Emits `artworkChanged(imageData)` for a song with an embedded cover;
    `MainWindow` connects it to `ISystemMediaInterface::setArtwork`.
* The two presenters do not know about each other; `MainWindow` drives both.
* `LyricsLoader` / `LyricsManager`
  * Lyrics fetch + timed line updates from playback position.
  * On play, `PlaybackController` refreshes song metadata once;
    `LyricsLoader::resolveLyrics(...)` then picks the first non-empty of the
    `attr:unsynced_lyrics` and `attr:lyrics` fields, then of the
    `unsynced_lyrics` and `lyrics` tag fields with no column. If none is set
    or it has no timestamped lines, the `.lrc` file is the fallback.
  * The `.lrc` fallback (`LyricsLoader::getLyrics`) opens
    `<home>/Music/<artist> - <title>.lrc` only, where `<home>` is
    `USERPROFILE` on Windows and `HOME` elsewhere; it does not look next to
    the audio file.
  * Known issue (Windows): the `.lrc` file name is built as a UTF-8
    `std::string` and converted to `std::filesystem::path`, which on Windows
    decodes narrow strings with the active ANSI code page, not UTF-8. A
    non-ASCII artist or title (for example CJK) maps to a different file name,
    so its `.lrc` is not found. macOS/Linux are unaffected, as is Windows with
    the system-wide UTF-8 code page enabled. Fix: build the path from UTF-8
    explicitly (`std::u8string` or a wide path), as `SongParser` does for
    TagLib with `MultiByteToWideChar`.
  * Cover art comes from `SongParser::extractCoverImage`; a file it cannot
    open (for example moved or deleted) logs a warning and shows no cover.
* Lyrics panel style settings (`SettingsDialog` + `LyricsPresenter`)
  * Uses four settings keys: `lyrics/use_system_default_font`,
    `lyrics/font_family`, `lyrics/font_size`, and
    `lyrics/highlight_color`.
  * `font_family` is always persisted, even when system default is enabled.
  * On dialog load, it tries stored `font_family`; if not selectable, falls
    back to combo index `0`.
  * When system default is enabled, the combo is disabled but still shows the
    stored custom family.
  * `highlight_color` is selected via color picker and applied to highlighted
    lyric lines.
  * Apply behavior:
    * `use_system_default_font=true`: system UI family + stored size
    * `use_system_default_font=false`: stored family + stored size
  * The lyrics font starts from the application font, and a stored family is
    put first in its family list, so the CJK fallback families below still
    apply to characters the chosen font lacks.

### UI styling

Toolbar icons, the Windows application font, and the light/dark theme.

* Playback toolbar icons
  * `mainwindow.ui` keeps playback controls in a nested
    `playbackButtonsLayout` so the button group has its own compact spacing
    before the volume and seek sliders.
  * `volumeSlider` sits between `playbackButtonsLayout` and the seek slider,
    persists to `playback/volume_percent`, defaults to `100`, and is reapplied
    when the active playback backend changes.
  * SVG assets live in `src/resources/icons` and are registered through
    `src/resources/resources.qrc`; third-party attribution is tracked in
    `THIRD_PARTY_NOTICES.md`.
  * `media-playback-random.svg` is a custom composite icon: play triangle on
    the left and question mark on the right.
  * `MainWindow` initializes the resource collection with
    `Q_INIT_RESOURCE(resources)` because the qrc is compiled into static
    library target `MYLib`.
  * Playback buttons render these icons as fixed-size `QToolButton` controls
    (`setIconOnlyPlaybackButton` in `playbackbuttonstyle.cpp`) and tint them
    at runtime (`#fcfcfc` in dark mode, `#232629` otherwise).
  * Tinted icon pixmaps are rendered at the displaying widget's device pixel ratio for
    sharp high-DPI output, but button/icon sizing stays in Qt logical pixels.
    The tint fill uses the logical icon rect after `setDevicePixelRatio(...)`
    so painting remains in the same coordinate space as the displayed icon.
* Application font and CJK fallback (Windows, `main.cpp`)
  * Without an explicit family list, Qt does not use Windows font fallback
    (neither the registry font-link list nor DirectWrite system fallback). It
    uses a built-in list chosen by user language
    (`QWindowsFontDatabaseBase::extraTryFontsForFamily`); for non-CJK locales
    that renders Chinese text in MS UI Gothic / SimSun.
  * Temporary fix: the app font uses the system UI family, then `Meiryo UI`,
    `Microsoft JhengHei UI`, `Microsoft YaHei UI`, the order of the `Segoe UI`
    entry in `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontLink\SystemLink`
    on an en-US install.
  * Limitation: Han characters shared with Japanese use Meiryo UI glyph
    shapes, and Simplified-only characters fall through to JhengHei / YaHei,
    so one line of Chinese can mix fonts (native Windows does the same on
    non-Chinese locales). Possible replacements: choose the CJK family order
    by detected text language, or use one CJK family with consistent design
    across languages (for example Source Han Sans).
  * Size: native Windows draws CJK fallback fonts larger than the base font
    (Segoe UI 9pt falls back to Meiryo UI 10pt). This is hard in Qt:
    `QFontEngineMulti` creates every fallback engine at the base font size,
    so it would need per-run layout in every place text is drawn (custom
    delegates, `QProxyStyle` overrides for menus). Instead the whole app font
    is set to 10pt on Windows.
  * 10pt also matches the `windows11` style's menu bar, which paints
    `CE_MenuBarItem` text at a hard-coded 10pt while sizing items from the
    widget font. With another app font size, menu bar gaps change but the
    text does not. Other sizes would need a `QProxyStyle` override of
    `CE_MenuBarItem` or the `windowsvista` style.
* Display theme (`SettingsDialog` + `MainWindow`)
  * Display theme mode is persisted as `display/theme_mode` with values:
    `system`, `light`, `dark`.
  * Theme mode switches `QStyleHints::colorScheme` (`system`/`light`/`dark`);
    app widget style remains the system/default style.

## Core Feature Implementation Notes

### Startup loading

How the library loads off the main thread, and what stays disabled until it
is ready.

* File-backed startup keeps library-dependent actions disabled until playlist
  hydration finishes and shows `Loading library...` in the status bar.
* The worker path creates its own `ColumnRegistry`, `DatabaseManager`, and
  `SongLibrary` with a separate SQLite connection, then returns a
  `SongLibrary::Snapshot`.
* The main thread applies the snapshot to the app-owned `SongLibrary`, loads
  playlists (`PlaylistCollection::loadAll()`), creates playlist tabs/models,
  re-enables actions, restores the normal status-bar message, and starts cloud
  sync.
* Custom (`attr:`) columns enter `ColumnRegistry` through
  `ColumnRegistry::loadDynamicColumns` (at the start of `finishSetUpPlaylist`,
  or in `setUpPlaylist` for `:memory:`), but the display expressions are
  parsed earlier, in the `MainWindow` constructor.
  Field names are resolved at parse time, so `finishSetUpPlaylist` re-parses
  them (`NowPlayingDisplay::loadExpressions()`); without that, a custom field
  showed its name inside `${}` (`[mood]` instead of `[happy]`), or the
  expression fell back to the default, until Settings was applied.
  See [expressions.md](expressions.md) for how display expressions resolve
  fields.
* Startup-sensitive handlers are guarded in addition to disabling actions, so
  direct/system-media requests cannot play, seek, load songs, or manually rebase
  before playlist hydration finishes.
* `:memory:` databases keep the synchronous path for tests because separate
  SQLite in-memory connections do not share state.

### Song ingest and metadata refresh

How files become songs, and how songs are refreshed after their tags change.

* Ingest path: parse file -> evaluate computed fields -> generate identity key -> upsert song -> sync dynamic/computed attributes.
  Adding files to a playlist (open, add folder) goes through
  `Playlist::addFiles(...)`, which runs this path for each file and then
  appends the rows.
* `SongParser::parse` returns `std::nullopt` when TagLib cannot open the file
  (missing, unreadable, or unsupported); a readable file without tags is not
  a failure. `Playlist::addFiles(...)` skips such paths and reports them
  through its optional `failedPaths` output. `MainWindow::open()` shows an
  error dialog only when the single selected file failed; multi-file and
  folder adds skip failed files silently.
* Refresh path re-parses file and syncs built-in/dynamic/computed fields to DB + memory.
  If parsing fails, the stored fields are kept unchanged.
* Computed values are namespaced in memory (`computed:<key>`) but persisted in
  DB with plain keys (`<key>`) in `song_computed_attributes`.
* Properties save path:
  * collect dirty/removed rows -> `SongParser::writeTags(...)` -> `SongLibrary::refreshSongFromFile(...)` -> emit song-updated signal -> `PlaylistCollection::notifySongChanged(...)` refreshes the song's rows.
* Two bulk user-triggered flows are intentionally blocking with progress UI:
  * `PlaylistTabs::refreshPlaylistMetadata(...)` (playlist-wide metadata refresh
    via `Playlist::refreshMetadataFromFiles(...)`)
  * `MainWindow::openFolder()` (folder import via `Playlist::addFiles(...)`)
  Both use modal progress dialogs and process events while work runs, instead of
  moving those operations to background threads.

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

### Play statistics

When a play counts, and where last-played and play counts are stored.

* `last_played_timestamp` is set at playback start.
* `play_count` increments once per play session with near-end + listened-duration gating.
  * `PlayCompletionTracker` decides when: the song reached
    `min(90%, end - 5 s)` and two thirds of its duration were listened to
    while playing; a seek adds no listened time. It keeps only the session
    and keeps the duration across sessions, since the backend may not
    report an unchanged duration again (replaying the same song).
  * `PlaybackController` reacts, on position updates and `EndOfMedia`:
    `SongLibrary::incrementPlayCount`, then `markCounted()`,
    `PlaylistCollection::notifySongChanged` and `playCounted(songPk)`, which
    `MainWindow` connects to the cloud push.
* Stats are stored by song identity, so multiple files with the same normalized identity share counters.

### Playlist persistence

What is saved per playlist, and which class writes each table.

* Playlist identity is `playlist_id`.
* Playlist order is persisted by `tab_order`.
* Last opened playlist tab is persisted in `QSettings` (`playlist/last_opened_id`).
* Per-playlist resume pointer is persisted by `last_played`. Play with nothing
  playing resumes it, or starts at the first row when that song is not in the
  playlist (new playlists start with `last_played = 1`, or the song was
  removed); an empty playlist does nothing.
* Playlist membership is persisted in `playlist_items(position)`.
* Playlist tab name edits are done inline on the tab bar and persisted to
  `playlists.name`.
* Writers: `PlaylistCollection` owns `playlists` rows (insert, delete, `name`,
  `tab_order`); `Playlist` owns `playlist_items` and `playlists.last_played`.
* Playlist metadata writes (`playlists` row create/update/delete) stay synchronous.
* Removing a row from a playlist also deletes its `playlist_items` row. Sort
  order is not persisted.
* New `playlist_items` rows get a position above every stored one, so
  positions may have gaps but never collide.

## Threading / Async Model

* Qt UI/model code runs on the main thread, and so do backend playlist and
  playback operations; their signals use direct connections.
* Cloud sync HTTP functions run synchronously on a coordinator-owned worker
  thread; results that touch `SongLibrary` or UI models are delivered back to
  the main thread.
* Cloud sync worker tasks are serialized through the worker thread event queue.
  See [cloud-sync.md](cloud-sync.md) for the sync design.
* GStreamer GLib loop uses a dedicated `std::thread` on macOS/Windows.
* Playlist DB writes, including the bulk `playlist_items` delete in
  `Playlist::clear()`, are synchronous, so rows appended right after a clear
  are never removed by a late delete.

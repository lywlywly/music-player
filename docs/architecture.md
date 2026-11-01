# Architecture and Implementation Notes

This file contains implementation-level details that are intentionally kept out
of `README.md`.

## Design Principles

### Backend classes do not leak UI

Backend classes hold app state and operations; frontend classes present them.
The backend must not depend on how (or whether) it is displayed.

* Backend: `SongLibrary`, `DatabaseManager`, `Playlist`, `PlaylistCollection`,
  `PlaybackQueue`, `PlaybackManager`, playback policies.
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

### App composition

* `MainWindow`
  * Wires the app’s major subsystems.
  * Owns playback actions, settings integration, and top-level UI flows.
  * Owns `PlaylistCollection`, created as a child `QObject` right after
    `ui->setupUi(...)`. Children are destroyed in creation order, so the UI
    (tabs, views, models) is destroyed before the playlists it shows.
  * Starts file-backed library loading on a worker thread during startup and
    hydrates UI state after the library snapshot is ready.

### Persistence and schema

* `DatabaseManager`
  * Owns SQLite connection, pragmas, and schema setup.
  * Creates tables for songs, playlists, dynamic/computed attributes, and play stats.

### Column and layout metadata

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

* `PlaybackQueue` (backend)
  * Current song pointer + explicit queued songs + playback status.
  * Owns per-song status: `statusOf(songPk, playlist)` returns Playing/Paused/None
    for the current song (queue position not consulted), otherwise Queued with
    its position for a song queued from the same playlist. `statusChanged(songPk,
    playlist)` reports changes.
  * Stores `Playlist` pointers but only compares them, never dereferences
    them. `removePlaylist(playlist)` drops that playlist's current song and
    queued entries; `MainWindow` calls it on
    `PlaylistCollection::playlistAboutToBeRemoved`, while the id still resolves
    to the `Playlist`.
* `PlaybackManager` (backend)
  * Playback policy orchestration (`next/prev/play/pause/stop`) on top of `PlaybackQueue` + active `Playlist`.
  * Requires `setView(Playlist&)` with a valid playlist before playback/policy operations.
  * Songs removed from a playlist leave stale ids behind: `next()` skips queued
    songs no longer in their playlist, and the policies treat a current song no
    longer in the playlist as no current song (sequential Next starts at the
    first row, Prev at the last).
* Playback policies (`PlaybackPolicySequential`, `PlaybackPolicyShuffle`)
  * `PlaybackPolicyShuffle` sizes its history to half the playlist and follows
    size changes through `Playlist` insert/remove/reset signals; it
    disconnects when switched to another playlist or destroyed.
  * Shuffle steps through history entries still in the playlist; otherwise it
    picks a random row whose song is not in the history. Songs are unique per
    playlist and the history holds at most half the rows, so a candidate
    always exists.
  * `Playback -> Playback follows cursor` passes the first selected row from
    `MainWindow` into `PlaybackManager::next(...)`: queued songs still take
    priority, then the selected row may be played once if it is not already
    current.
  * `Playback -> Cursor follows playback` gates whether playback-driven song
    changes call `PlaylistTabs::navigateIndex(...)` to select and scroll to the
    new row.
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
  * Transport and volume control consumed by `MainWindow`.
  * Exposes `bitrateChanged(bitsPerSecond)` for playback-time bitrate updates.
  * `AudioPlayer` stores clamped volume as an integer percentage (`0..100`);
    `QtAudioPlayer` maps it to `QAudioOutput::setVolume(...)`, and
    `GstAudioPlayer` maps it to the GStreamer `playbin` `volume` property.

### Search expression domain

* `libraryexpression_*` modules
  * Tokenization, parsing, AST, type inference, and operator evaluation.
  * Supports boolean expressions, comparisons, lists/ranges, and `IF ... THEN ... ELSE ...`.
  * Supports string interpolation with backticks and `${...}` placeholders.
  * Resolves names through `ExprSymbolResolver` into canonical namespaced ids:
    * runtime: `status:<name>`
    * built-in: `builtin:<name>`
    * dynamic tag: `attr:<key>`
    * computed: `computed:<key>`
  * Symbol providers export both unqualified aliases and fully-qualified names.
  * Unqualified resolution precedence is: `status > builtin > attr > computed`.
  * Precedence is implemented by first-match lookup in
    `ExprSymbolResolver::lookup(...)` over merged symbol order:
    display contexts prepend runtime symbols, while registry symbols are emitted
    in `builtin`, then `attr`, then `computed` order.
  * `HAS` supports multi-value text split by comma separators.
* `LibraryExprEvalContext`
  * Evaluation boundary for field lookup.
  * Provides `FieldValue` instances to the expression runtime.
* `LibrarySearchDialog` + `LibrarySearchResultsModel`
  * Parse/evaluate query via `SongLibrary`, then present matching rows.

### Cloud play-count sync

* `CloudPlayStatsSync`
  * Stateless blocking HTTP functions for Lambda APIs (pull/push/bulk-push +
    throttle retry). They are called from the cloud-sync worker thread, not the
    UI thread.
* `CloudPlayStatsSyncCoordinator`
  * Sync policy layer: startup incremental/rebase pull, `max(local, cloud)`
    merge, rebase delta push, and affected-song UI notifications.
  * Owns a dedicated `QThread` plus a generic worker `QObject`; cloud tasks are
    posted to that worker with `Qt::QueuedConnection` so UI calls return
    immediately and cloud operations run serially.
  * Marshals results that touch `SongLibrary`, settings cursors, or UI-facing
    signals back to the main thread.
  * Startup sync starts only after playlist/library hydration completes.

### System media and lyrics

* `ISystemMediaInterface` (+ platform impls)
  * Bridge to OS media controls/metadata.
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
    and tint them at runtime (`#fcfcfc` in dark mode, `#232629` otherwise).
  * Tinted icon pixmaps are rendered at the displaying widget's device pixel ratio for
    sharp high-DPI output, but button/icon sizing stays in Qt logical pixels.
    The tint fill uses the logical icon rect after `setDevicePixelRatio(...)`
    so painting remains in the same coordinate space as the displayed icon.
* `LyricsLoader` / `LyricsManager`
  * Lyrics fetch + timed line updates from playback position.
  * On play, `MainWindow` refreshes song metadata once and prefers embedded
    lyrics from parsed tag fields; file-based `.lrc` loading is fallback.
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
* Lyrics panel style settings (`SettingsDialog` + `MainWindow`)
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
* Display settings (`SettingsDialog` + `MainWindow`)
  * Display theme mode is persisted as `display/theme_mode` with values:
    `system`, `light`, `dark`.
  * Theme mode switches `QStyleHints::colorScheme` (`system`/`light`/`dark`);
    app widget style remains the system/default style.
  * Status bar expression is persisted as `status_bar/expression`.
  * Window title expression is persisted as `window_title/expression`.
  * Settings dialog shows a live preview for the currently active display
    expression editor, using current playback/runtime song context when
    available (fallback sample values otherwise).
  * Default expression comes from `StatusRuntimeSymbolTable::defaultStatusBarExpression()`.
  * Default window title expression comes from
    `StatusRuntimeSymbolTable::defaultWindowTitleExpression()`.
  * `StatusRuntimeSymbolTable` also registers its runtime field definitions into
    `FieldTypePool`, so runtime fields (for example `playback_time` and
    `duration`) use the same `FieldValue::display()` formatting pipeline.
  * Runtime symbols are exposed as `status:*` fields
    (`status:isplaying`, `status:ispaused`, `status:playback_time`,
    `status:duration`, `status:bitrate`) and also as unqualified aliases.
  * Runtime symbols are only available in display-expression parsing/evaluation
    contexts; library-search parsing uses registry symbols only.

## Core Feature Implementation Notes

### Startup loading

* File-backed startup keeps library-dependent actions disabled until playlist
  hydration finishes and shows `Loading library...` in the status bar.
* The worker path creates its own `ColumnRegistry`, `DatabaseManager`, and
  `SongLibrary` with a separate SQLite connection, then returns a
  `SongLibrary::Snapshot`.
* The main thread applies the snapshot to the app-owned `SongLibrary`, loads
  playlists (`PlaylistCollection::loadAll()`), creates playlist tabs/models,
  re-enables actions, restores the normal status-bar message, and starts cloud
  sync.
* Startup-sensitive handlers are guarded in addition to disabling actions, so
  direct/system-media requests cannot play, seek, load songs, or manually rebase
  before playlist hydration finishes.
* `:memory:` databases keep the synchronous path for tests because separate
  SQLite in-memory connections do not share state.

### Song ingest and metadata refresh

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

* `last_played_timestamp` is set at playback start.
* `play_count` increments once per play session with near-end + listened-duration gating.
* Stats are stored by song identity, so multiple files with the same normalized identity share counters.

### Bitrate display behavior

* `MainWindow` evaluates both status bar and window title from display
  expressions (same expression engine + runtime symbols).
* `${bitrate}` in display expressions reads runtime symbol `status:bitrate`.
* `status:bitrate` is updated from `MainWindow::effectiveBitrateKbps()`:
  * if track is treated as CBR and parsed tag bitrate is available, use tag bitrate
  * otherwise use runtime playback bitrate when available
  * otherwise fall back to parsed tag bitrate (or `0`)
* `GstAudioPlayer` provides playback-time bitrate:
  * hooks `playbin` `element-setup`,
  * attaches one pad probe to the first `Decoder/Audio` sink pad,
  * accumulates buffer bytes in an atomic counter,
  * emits `bitrateChanged` about once per second from `updatePosition()`.
* For tracks treated as CBR (`mp3` and `wav`, detected via TagLib file type),
  status display prefers parsed tag bitrate from song metadata.
* `QTAudioPlayer` does not currently emit runtime bitrate updates, so UI falls
  back to tag bitrate when present.

### Expression parse/eval context split

* `QString` / `std::string` boundary:
  * `QString`: tokenizer/parser input, token text, and parse errors.
  * `std::string`: AST/runtime/eval/storage data (`ExprValue`,
    `ExprFieldRef`, runtime values, interpolated parts, `FieldValue::text`,
    eval lookup key `std::string_view`).
  * Main conversion happens during parse, where token text is normalized and
    resolved into `std::string` AST/runtime payloads.

* Library search:
  * Resolver source: `ColumnRegistry::expressionSymbols()`
  * Eval context: song-only (`SongLibraryExprEvalContext`)
  * Eval lookup maps canonical IDs to song map keys:
    * `builtin:* -> <builtin key>`
    * `attr:* -> attr:*`
    * `computed:* -> computed:*`
* Display expressions (status bar + window title):
  * Resolver source: runtime symbols merged first, then registry symbols
  * Eval context: runtime-first, then current song (`DisplayExpressionEvalContext`)
  * Collisions prefer runtime symbols; fully-qualified names always disambiguate.
  * Interpolation is supported in backtick strings only.
  * In interpolation, `${field}` display comes from `FieldValue::display()`.
* Comparison AST/eval shape is unified as `leftExpr op rightExpr`.
  * The comparison type is resolved from the left expression:
    field refs use field value type; non-field expressions use static type.
  * Right side is validated against that left-side type (either literal/list/range
    value form or another expression).
* `ExprValueExpr` wraps RHS value-form literals (scalar/list/range) parsed by
  `parseValue(...)`, so comparisons keep one `leftExpr op rightExpr` shape.
* Field resolution/evaluation split:
  * Parse resolves field tokens into canonical namespaced `resolvedId`s.
  * Unqualified names are canonicalized to fully-qualified IDs at parse time
    using precedence; evaluation does not re-resolve by precedence.
  * Eval always reads by canonical ID (`context.fieldValue(resolvedId)`).
  * In display context, canonical `status:*` fields are read from runtime
    symbols; other canonical fields are read from the current song.
* Parser flow (high level):
  * `parseOr(...)` is the parse entry point, then `parseAnd(...)`,
    `parseUnary(...)`, and `parsePrimary(...)` by precedence.
  * `parsePrimary(...)` parses an atom first, then optional comparison suffix.
    If atom-start is invalid, it falls back to field-comparison parsing for
    clearer diagnostics.
  * Field-led (`field op value`) and generic suffix (`expr op value`)
    comparisons share the same comparison-tail parser path for consistency:
    operator parse in `parseComparisonTail(...)`,
    RHS parse+validation in `parseComparisonRightExpr(...)`,
    then `ComparisonExpr` construction.

### Playlist persistence

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

## Cloud Sync Design

### Cursor and Rebase

* `last_synced_at` is stored in `QSettings` at key `cloud_sync/last_synced_at`.
* Incremental pull uses `updated_after = max(0, last_synced_at - 60)`.
* After successful incremental pull, `last_synced_at` advances to
  `max(maxUpdatedAtFromPages, now)`.
* UUID change (on Settings `OK`) sets `rebase_pending=true` and resets
  `last_synced_at=0`.
* On UUID change, `MainWindow` also triggers manual rebase immediately after
  settings apply, so rebase starts right away (not only next startup).
* Main window startup runs sync immediately:
  * if `rebase_pending=true`, it runs rebase first
  * otherwise it runs incremental pull
* Manual menu action (`Library -> Manual cloud rebase`) triggers rebase when a
  valid UUID is set.
* Rebase success clears `rebase_pending` and sets `last_synced_at=now`.
* If a pending rebase fails (pull or push), `rebase_pending` stays `true` and
  startup will retry later. Manual rebase does not set this flag by itself.

### DynamoDB Data Model

* Table name: `play_stats`
* Primary key:
  * partition key: `user_uuid` (String)
  * sort key: `song_identity_key` (String)
* Attributes:
  * `play_count` (Number)
  * `updated_at` (Number, unix seconds)
* Pull GSI:
  * name: `gsi_updated_at`
  * partition key: `user_uuid` (String)
  * sort key: `updated_at` (Number)
  * projection: `ALL`

### Lambda API Surface

* `GET` pull:
  * params: `user_uuid`, optional `updated_after`, `limit`,
    `last_evaluated_key`
  * returns: `items` and `next_last_evaluated_key`
  * reads from `gsi_updated_at`
* `POST` single increment:
  * body: `{ user_uuid, song_identity_key, delta }`
* `POST` bulk increment:
  * body: `{ user_uuid, updates: [{ song_identity_key, delta }, ...] }`
  * duplicate keys in one batch are merged server-side before update
* `OPTIONS`: CORS preflight

### Current Pacing and Batch Settings

* Pull page size: `100`
* Pull inter-page gap: `1s`
* Rebase bulk push chunk size: `10`
* Rebase inter-chunk gap: `1s`
* Pull/bulk API validation enforces `limit <= 200` and `updates <= 200`.
* Throttle handling (lambda):
  * botocore retry mode `adaptive`, max attempts `10`
* Throttle handling (client):
  * retry after `60s`
  * request transfer timeout: `15s`
  * max client retries for throttle/timeout: `3`

## Threading / Async Model

* Qt UI/model code runs on the main thread, and so do backend playlist and
  playback operations; their signals use direct connections.
* Cloud sync HTTP functions run synchronously on a coordinator-owned worker
  thread; results that touch `SongLibrary` or UI models are delivered back to
  the main thread.
* Cloud sync worker tasks are serialized through the worker thread event queue.
* Cloud pull inter-page waits and rebase-push chunking are paced by `QTimer`.
* GStreamer GLib loop uses a dedicated `std::thread` on macOS/Windows.
* Playlist DB writes, including the bulk `playlist_items` delete in
  `Playlist::clear()`, are synchronous, so rows appended right after a clear
  are never removed by a late delete.

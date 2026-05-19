# Architecture and Implementation Notes

This file contains implementation-level details that are intentionally kept out
of `README.md`.

## Main Classes and Responsibilities

### App composition

* `MainWindow`
  * Wires the app’s major subsystems.
  * Owns playback actions, settings integration, and top-level UI flows.
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

* `PlaylistTabs`
  * Owns playlist metadata behavior (`playlists` table): create/delete playlist rows, restore/reorder tabs, repair invalid `tab_order`.
  * Creates one `Playlist` per tab and connects UI interactions.
  * Opens per-song Properties dialog from row context menu and refreshes playlist rows after successful save.
* `Playlist`
  * `QAbstractTableModel` adapter for one playlist.
  * Delegates storage/order to `SongStore` and renders status column from `PlaybackQueue`.
* `SongStore`
  * Per-playlist song-id ordering and sorting.
  * Owns `playlist_items` persistence.

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

* `PlaybackQueue`
  * Current song pointer + explicit queued songs.
* `PlaybackManager`
  * Playback policy orchestration (`next/prev/play/pause/stop`) on top of `PlaybackQueue` + active `Playlist`.
  * Requires `setView(Playlist&)` with a valid playlist before playback/policy operations.
* `PlaybackBackendManager`
  * Runtime backend owner/switcher (`QMediaPlayer` or `GStreamer`).
  * Manages GLib loop thread for GStreamer on macOS/Windows.
  * `MainWindow` prewarms GStreamer startup when GStreamer is the saved backend:
    `gst_init` on all platforms and default output device lookup on macOS.
* `AudioPlayer` (+ concrete backends)
  * Transport + media events consumed by `MainWindow`.
  * Exposes `bitrateChanged(bitsPerSecond)` for playback-time bitrate updates.

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
* `LyricsLoader` / `LyricsManager`
  * Lyrics fetch + timed line updates from playback position.
  * On play, `MainWindow` refreshes song metadata once and prefers embedded
    lyrics from parsed tag fields; file-based `.lrc` loading is fallback.
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
* The main thread applies the snapshot to the app-owned `SongLibrary`, creates
  playlist tabs/models, re-enables actions, restores the normal status-bar
  message, and starts cloud sync.
* Startup-sensitive handlers are guarded in addition to disabling actions, so
  direct/system-media requests cannot play, seek, load songs, or manually rebase
  before playlist hydration finishes.
* `:memory:` databases keep the synchronous path for tests because separate
  SQLite in-memory connections do not share state.

### Song ingest and metadata refresh

* Ingest path: parse file -> evaluate computed fields -> generate identity key -> upsert song -> sync dynamic/computed attributes.
* Refresh path re-parses file and syncs built-in/dynamic/computed fields to DB + memory.
* Computed values are namespaced in memory (`computed:<key>`) but persisted in
  DB with plain keys (`<key>`) in `song_computed_attributes`.
* Properties save path:
  * collect dirty/removed rows -> `SongParser::writeTags(...)` -> `SongLibrary::refreshSongFromFile(...)` -> emit song-updated signal for playlist model refresh.
* Two bulk user-triggered flows are intentionally blocking with progress UI:
  * `PlaylistTabs::refreshPlaylistMetadata(...)` (playlist-wide metadata refresh)
  * `MainWindow::openFolder()` (folder import)
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
* Per-playlist resume pointer is persisted by `last_played`.
* Playlist membership is persisted in `playlist_items(position)`.
* Playlist tab name edits are done inline on the tab bar and persisted to
  `playlists.name`.
* Playlist metadata writes (`playlists` row create/update/delete) stay synchronous.

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

* Qt UI/model code runs on the main thread.
* Cloud sync HTTP functions run synchronously on a coordinator-owned worker
  thread; results that touch `SongLibrary` or UI models are delivered back to
  the main thread.
* Cloud sync worker tasks are serialized through the worker thread event queue.
* Cloud pull inter-page waits and rebase-push chunking are paced by `QTimer`.
* GStreamer GLib loop uses a dedicated `std::thread` on macOS/Windows.
* DB writes that can touch many songs are backgrounded. Current path:
  `SongStore::removePlaylistItemsInDb()` (bulk `playlist_items` delete) runs in
  a worker thread for file-backed DBs and synchronously for in-memory DB tests.

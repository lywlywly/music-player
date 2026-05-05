# Architecture and Implementation Notes

This file contains implementation-level details that are intentionally kept out
of `README.md`.

## Main Classes and Responsibilities

### App composition

* `MainWindow`
  * Wires the app’s major subsystems.
  * Owns playback actions, settings integration, and top-level UI flows.

### Persistence and schema

* `DatabaseManager`
  * Owns SQLite connection, pragmas, and schema setup.
  * Creates tables for songs, playlists, dynamic/computed attributes, and play stats.

### Column and layout metadata

* `ColumnRegistry`
  * Source of truth for built-in, dynamic, and computed column definitions.
* `GlobalColumnLayoutManager`
  * Stores visible order/width/visibility for columns across playlist/search views.

### Song domain

* `SongLibrary`
  * Canonical song repository in memory + DB sync.
  * Loads built-in + dynamic + computed + play-stats data.
  * Parses files (`loadSongFromFile`), upserts song rows (`addTolibrary`), refreshes metadata, and evaluates computed fields.
  * Evaluates search expressions.
  * Maintains identity-based play stats using normalized `title|artist|album`.

### Playlist domain

* `PlaylistTabs`
  * Owns playlist metadata behavior (`playlists` table): create/delete playlist rows, restore/reorder tabs, repair invalid `tab_order`.
  * Creates one `Playlist` per tab and connects UI interactions.
* `Playlist`
  * `QAbstractTableModel` adapter for one playlist.
  * Delegates storage/order to `SongStore` and renders status column from `PlaybackQueue`.
* `SongStore`
  * Per-playlist song-id ordering and sorting.
  * Owns `playlist_items` persistence (append/clear/load by playlist).

### Playback domain

* `PlaybackQueue`
  * Current song pointer + explicit queued songs.
* `PlaybackManager`
  * Playback policy orchestration (`next/prev/play/pause/stop`) on top of `PlaybackQueue` + active `Playlist`.
  * Requires `setView(Playlist&)` with a valid playlist before playback/policy operations.
* `PlaybackBackendManager`
  * Runtime backend owner/switcher (`QMediaPlayer` or `GStreamer`).
  * Manages GLib loop thread for GStreamer on macOS/Windows.
* `AudioPlayer` (+ concrete backends)
  * Transport + media events consumed by `MainWindow`.

### Search expression domain

* `libraryexpression_*` modules
  * Tokenization, parsing, AST, static type inference, and operator evaluation.
  * Supports boolean expressions, comparisons, lists/ranges, and `IF ... THEN ... ELSE ...`.
* `LibrarySearchDialog` + `LibrarySearchResultsModel`
  * Parse/evaluate query via `SongLibrary`, then present matching rows.

### Cloud play-count sync

* `CloudPlayStatsSyncService`
  * Thin async HTTP client for Lambda APIs (pull/push/bulk-push + throttle retry).
* `CloudPlayStatsSyncCoordinator`
  * Sync policy layer:
    * startup incremental pull or rebase
    * merge rule `max(local, cloud)`
    * rebase delta push when local is ahead
    * emit affected song PKs for UI refresh

### System media and lyrics

* `ISystemMediaInterface` (+ platform impls)
  * Bridge to OS media controls/metadata.
* `LyricsLoader` / `LyricsManager`
  * Lyrics fetch + timed line updates from playback position.

## Core Feature Implementation Notes

### Song ingest and metadata refresh

* Ingest path: parse file -> evaluate computed fields -> generate identity key -> upsert song -> sync dynamic/computed attributes.
* Refresh path re-parses file and syncs built-in/dynamic/computed fields to DB + memory.
* Two bulk user-triggered flows are intentionally blocking with progress UI:
  * `PlaylistTabs::refreshPlaylistMetadata(...)` (playlist-wide metadata refresh)
  * `MainWindow::openFolder()` (folder import)
  Both use modal progress dialogs and process events while work runs, instead of
  moving those operations to background threads.

### Play statistics

* `last_played_timestamp` is set at playback start.
* `play_count` increments once per play session with near-end + listened-duration gating.
* Stats are stored by song identity, so multiple files with the same normalized identity share counters.

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
* Rebase success clears `rebase_pending` and sets `last_synced_at=now`.
* If rebase pull/push fails, `rebase_pending` remains `true` and retries later.

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
* Throttle handling (lambda):
  * botocore retry mode `adaptive`, max attempts `10`
* Throttle handling (client):
  * retry after `60s`

## Threading / Async Model

* Qt UI/model code runs on the main thread.
* Cloud sync HTTP is asynchronous via `QNetworkAccessManager` callbacks.
* Cloud pull paging and rebase-push chunking are paced by `QTimer`.
* GStreamer GLib loop uses a dedicated `std::thread` on macOS/Windows.
* DB writes that can touch many songs are backgrounded. Current path:
  `SongStore::removePlaylistItemsInDb()` (bulk `playlist_items` delete) runs in
  a worker thread for file-backed DBs and synchronously for in-memory DB tests.

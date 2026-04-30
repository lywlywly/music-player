# myplayer

A cross-platform desktop music player built with Qt, focused on local library playback, lyrics support, and OS media integration.

## Motivation

* foobar2000 is an excellent music player that meets most of my requirements. However, it lacks a dedicated Linux desktop version (although it can be run through Wine), and its macOS version is not fully featured. While DeaDBeeF is a viable alternative, it falls short in terms of available plugins compared to foobar2000, particularly the foo_openlyrics plugin.
* Consequently, I decide to implement a music player that meet my needs. Hope that I can make a player that is both well written and provides good user experience.

## macOS Qt Creator Troubleshooting

Qt Creator may set `DYLD_LIBRARY_PATH` in the Run Environment. On macOS, this can cause system image decode paths used by Media Center artwork/now-playing integration to load Homebrew codec libraries (for example `/opt/homebrew/Cellar/libpng/...`) and lead to runtime crashes.

If issues occur, open Qt Creator `Projects -> Run Settings -> Environment` and remove or unset `DYLD_LIBRARY_PATH` to avoid ImageIO/PNG decode crashes.

## Windows (vcpkg)

If building on Windows with vcpkg, install GStreamer with:

```powershell
.\vcpkg install gstreamer[core,plugins-base,plugins-good,mpg123,flac,ogg,libav,vorbis]
```

Set `GST_PLUGIN_PATH` as:

* Release: `<vcpkg-root>\installed\x64-windows\plugins\gstreamer`
* Debug: `<vcpkg-root>\installed\x64-windows\debug\plugins\gstreamer`

## Library Search Query Syntax

The library search dialog supports a compact query language for filtering songs.

Boolean operators:
* `AND`
* `OR`
* `NOT`

Grouping and precedence:
1. `( ... )`
2. `NOT`
3. `AND`
4. `OR`

Comparison operators:
* `IS` / `=`
* `HAS`
* `IN`
* `<`, `<=`, `>`, `>=`

Value forms:
* scalar: `field IS value`
* list: `field IN [a, b, c]`
* range (inclusive): `field IN [1..5]`

Rules:
* `=` is an alias for `IS`.
* `HAS` expects a scalar value.
* `IN` supports lists for all field types and ranges for numeric/datetime fields.
* Text matching is normalized (case-insensitive style matching).
* `HAS` splits TagLib-style multi-value text on ` / `.
* Typed fields (number/datetime/boolean) use typed comparison; invalid conversions fail parsing.

Examples:

```text
artist IS some artist
genre HAS rock AND NOT title HAS demo
artist IS a AND (genre HAS pop OR genre HAS rock)
genre IN [pop, rock, jazz]
tracknumber >= 2
date IN [2024-01-01..2025-12-31]
```

## Play Stats and Cloud Sync

Local playback stats:
* `play_count` increments once per play session only after near-end threshold is reached and listened time reaches at least `2/3` of track duration.
* `last_played_timestamp` is updated when playback starts.
* Stats are identity-based (normalized `title|artist|album`), so same-identity songs share counters.

Cloud sync:
* Sync target is `play_count` (UUID-scoped user data).
* On startup, app starts cloud sync:
  * if `rebase_pending=true`, run pull-first rebase.
  * otherwise run incremental pull with `updated_after = max(0, last_synced_at - 60)`.
* Merge rule is `max(local, cloud)` per song identity.
* On local completed-play increment, app pushes `delta=1` asynchronously (best effort).
* When UUID is changed in Settings and confirmed with `OK`, app triggers rebase immediately (for non-empty UUID).
* If rebase pull/push fails, `rebase_pending` stays `true` and retry happens on next startup.
* Rebase push is local-only positive delta: after pull/merge, app pushes only identities where `local_count > cloud_count` (`delta = local - cloud`).
* Settings behavior: `Set UUID`, `Start as New User`, and `Disable Cloud Sync` are staged in the dialog and only persisted when clicking `OK`; clicking `Cancel` discards them.
* Library menu has `Manual Cloud Rebase`; it is enabled only when UUID is valid.

### Cloud Sync Design

#### Cursor and Rebase
* `last_synced_at` is stored in `QSettings` at key `cloud_sync/last_synced_at`.
* Incremental pull uses `updated_after = max(0, last_synced_at - 60)`.
* After successful incremental pull, `last_synced_at` is advanced to `max(maxUpdatedAtFromPages, now)`.
* UUID change (on Settings `OK`) sets `rebase_pending=true` and resets `last_synced_at=0`.
* Rebase success clears `rebase_pending` and sets `last_synced_at=now`.
* If rebase pull/push fails, `rebase_pending` remains `true` and will retry later.

#### DynamoDB Data Model
* Table name: `play_stats`
* Primary key:
  * partition key: `user_uuid` (String)
  * sort key: `song_identity_key` (String)
* Attributes:
  * `play_count` (Number)
  * `updated_at` (Number, unix seconds)
* GSI used for pull:
  * name: `gsi_updated_at`
  * partition key: `user_uuid` (String)
  * sort key: `updated_at` (Number)
  * projection: `ALL`

#### Lambda API Surface
* `GET` (pull):
  * params: `user_uuid`, optional `updated_after`, `limit`, `last_evaluated_key`
  * returns: `items` + `next_last_evaluated_key`
  * reads from `gsi_updated_at`
* `POST` single increment:
  * body: `{ user_uuid, song_identity_key, delta }`
* `POST` bulk increment:
  * body: `{ user_uuid, updates: [{ song_identity_key, delta }, ...] }`
  * duplicate keys in a batch are merged server-side before update
* `OPTIONS`: CORS preflight

#### Current Pacing and Batch Settings
* Pull page size: `100`
* Pull inter-page gap: `1s` (successful page to next page)
* Rebase bulk push chunk size: `10`
* Rebase inter-chunk gap: `1s`
* Throttle handling (lambda):
  * DynamoDB client uses botocore retry mode `adaptive`, max attempts `10`
* Throttle handling (client):
  * on throttle response, retry same request after `60s`

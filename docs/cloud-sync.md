# Cloud Play-Count Sync

Optional sync of `play_count` per user UUID through AWS Lambda
(`infra/aws/lambda/play_stats_handler.py`) and DynamoDB. The client is part
of the Play Stats plugin ([plugins/playstats.md](plugins/playstats.md)), in
`src/plugins/playstats/`; its local side is the plugin's file
(`PlayStatsStore`).

## Client classes

* `CloudPlayStatsSync`
  * Stateless blocking HTTP functions for Lambda APIs (pull/push/bulk-push +
    throttle retry). They are called from the cloud-sync worker thread, not the
    UI thread.
* `CloudPlayStatsSyncCoordinator`
  * Sync policy layer: startup incremental/rebase pull, `max(local, cloud)`
    merge (`PlayStatsStore::mergeCloudCounts`, one transaction per page),
    rebase delta push, and the changed keys reported to the host
    (`ISongFieldHost::valuesChanged`), which refreshes the songs.
  * Owns a dedicated `QThread` plus a generic worker `QObject`; cloud tasks are
    posted to that worker with `Qt::QueuedConnection` so UI calls return
    immediately and cloud operations run serially.
  * Marshals results that touch the store, settings cursors or the host back
    to the main thread.
  * Created in the plugin's `initialize` and destroyed in its `shutdown`,
    which stops the thread. Startup sync is queued with a zero-delay timer
    and runs at the event loop's first turn, after the window shows. It
    does not wait for the library load, as it writes only the plugin's
    file; its `valuesChanged` reaches the songs after the load
    ([plugins/playstats.md](plugins/playstats.md), "What `SongLibrary`
    keeps").
* Each counted play pushes a single increment (`delta=1`) with a valid UUID.
  Its result is ignored: a failed push is not retried, so the cloud stays
  behind until a rebase, which pushes `local - cloud` for each key where
  that is positive. Rejected: a queue of unsent plays, kept on disk; a rare
  lost push does not justify it, and the rebase fixes it.
* Cloud pull inter-page waits and rebase-push chunking are paced by `QTimer`.

## Cursor and Rebase

* `last_synced_at` is stored in `QSettings` at key `playstats/last_synced_at`.
* Incremental pull uses `updated_after = max(0, last_synced_at - 60)`.
* After successful incremental pull, `last_synced_at` advances to
  `max(maxUpdatedAtFromPages, now)`.
* The UUID is stored at `playstats/user_uuid`; sync runs only with a valid
  UUID. The keys and the UUID check are in `CloudSyncSettings`
  (`cloudsyncsettings.h`), shared by the coordinator and the Settings page,
  so the page does not depend on the coordinator.
* While no UUID is stored, the Play Stats page offers a new one, saved on
  `OK`, unless `playstats/disabled_by_user` is set: the page sets it when
  the user clears the UUID, so sync stays off until they set one.
* On Settings `OK` (the Play Stats page, `PlayStatsSettingsPage`), a changed
  UUID (or a changed `playstats/disabled_by_user`) sets
  `rebase_pending=true` and resets `last_synced_at=0`; clearing the UUID
  sets `rebase_pending=false`.
* On UUID change, the page also sends `uuidChanged` as it applies, and the
  plugin starts a rebase right away (not only next startup).
* Startup runs sync at once:
  * if `rebase_pending=true`, it runs rebase first
  * otherwise it runs incremental pull
* The menu item (`Playback -> Play Stats -> Manual Cloud Rebase`) triggers
  rebase; it is enabled while a valid UUID is set.
* The rebase's local counts are every row of the plugin's file.
* Rebase success clears `rebase_pending` and sets `last_synced_at=now`.
* If a pending rebase fails (pull or push), `rebase_pending` stays `true` and
  startup will retry later. Manual rebase does not set this flag by itself.

## DynamoDB Data Model

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

## Lambda API Surface

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

## Current Pacing and Batch Settings

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

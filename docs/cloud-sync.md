# Cloud Play-Count Sync

Optional sync of `play_count` per user UUID through AWS Lambda
(`infra/aws/lambda/play_stats_handler.py`) and DynamoDB. See
[architecture.md](architecture.md) for the rest of the app.

## Client classes

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
* Cloud pull inter-page waits and rebase-push chunking are paced by `QTimer`.

## Cursor and Rebase

* `last_synced_at` is stored in `QSettings` at key `cloud_sync/last_synced_at`.
* Incremental pull uses `updated_after = max(0, last_synced_at - 60)`.
* After successful incremental pull, `last_synced_at` advances to
  `max(maxUpdatedAtFromPages, now)`.
* The UUID is stored at `cloud_sync/user_uuid`; sync runs only with a valid
  UUID.
* On Settings `OK`, a changed UUID (or a changed
  `cloud_sync/disabled_by_user`) sets `rebase_pending=true` and resets
  `last_synced_at=0`; clearing the UUID sets `rebase_pending=false`.
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

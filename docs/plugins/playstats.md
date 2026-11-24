# Plan: Play stats plugin

Status: implemented.

Play counts, last played times and their cloud sync are a plugin with its own
database. The app keeps no play stats and no song identity; it has a generic
way for a plugin to provide song fields, which columns, sorting, expressions
and autoplaylists use like any other field. The plugin system this extends is
in [plugin-system.md](plugin-system.md).

## Summary

* **The plugin** (`src/plugins/playstats/`, id `playstats`, name "Play
  Stats") decides when a play counts, stores `play_count` and
  `last_played_timestamp` per song identity in its own SQLite file, and
  syncs the counts with the cloud.
* **Its fields reach the host through a new capability**,
  `ISongFieldProvider`: the plugin declares its fields and the song fields
  its grouping key is computed from, turns those into a key, and gives the
  values for a batch of keys or for all of them. `SongLibrary` keeps a copy
  of the values on each song, so everything that reads song fields works
  unchanged. The plugin reports changed keys through a new host service,
  `ISongFieldHost::valuesChanged(keys)`; songs added, re-tagged songs and
  those changes all go through one batched refresh.
* **Each plugin gets its own `IPluginHost`**, so the host knows which plugin
  calls `dataFolder()` and `valuesChanged`.
* **Playback reaches the plugin through new listener events**:
  `durationChanged`, `paused`, `resumed`, `seeked`, `finished`.
* **No I/O while plugins load**: `initialize` and the capability calls made
  while the window is built read and write no file, database or network.
  The plugin's rows are read on the library load's worker thread.
* **Not in the app**: no identity or play stats tables, no identity in
  `SongLibrary`, no counting in `PlaybackController`, no cloud sync.
* **The identity key rule is the one the cloud stores** (normalized
  `title|artist|album`), so the cloud's data stays valid.

## Why a plugin

* Play stats and cloud sync are optional features. In the core they would
  touch the schema, `SongLibrary`, `PlaybackController`, `MainWindow` and
  Settings; as a plugin they can be disabled (no counting, no network, no
  columns) and change without touching the app's schema.
* Identity exists only for play stats. With them in the plugin, the app has
  no concept it does not use.
* Cost: a new capability, new host services, one host object per plugin and
  five listener events (see "Costs").

## Storage

### The plugin's database

`<data folder>/playstats.db`, where the data folder is
`IPluginHost::dataFolder()`:

```sql
CREATE TABLE song_play_stats (
    song_identity_key     TEXT PRIMARY KEY,
    play_count            INTEGER NOT NULL DEFAULT 0,
    last_played_timestamp INTEGER NOT NULL DEFAULT 0
) WITHOUT ROWID;
```

* **Keyed by the identity key, not an id.** The key is what the plugin
  computes from a song's tags and what the cloud stores, so local and cloud
  name an identity the same way, and nothing stored can disagree with the
  tags. Rejected: an integer `identity_id` with a link on each song (the
  app's design before the plugin): the link would be a foreign key into another file,
  which SQLite cannot enforce, and it is derived data the app has no use
  for. Cost: a text primary key, about 40 bytes per identity, stored once.
* **Rows are never deleted.** A row outlives the songs that have its key,
  so a file removed and added again gets its stats back.
  Cost: rows for songs that are gone accumulate; each is small.
* **No in-memory copy.** The file is the plugin's only store; each
  operation is one statement (below). A cache would have to be loaded before
  the host's fill and kept in step with the file; reads after startup are
  for a batch of keys, a primary-key lookup each (about 5–20 µs).
* **Its own schema version** (`PRAGMA user_version`, starting at 1), so a
  later change has a migration path inside the plugin. WAL,
  `synchronous = NORMAL` and a `busy_timeout`, as the app's database.
* **One connection per thread, one writer of rows.** The main thread's
  connection writes every row and serves the reads made there. A read made
  off the main thread (`allValues` on the library load's worker) opens a
  short-lived connection of its own. WAL lets that read run beside a write
  and see a consistent snapshot.
* **One way to open the file**, used by every connection: when the file does
  not exist, a read returns no rows without opening it (opening would create
  it); otherwise the schema is created or migrated inside `BEGIN IMMEDIATE`,
  after checking `user_version`. SQLite serializes those transactions, so
  whichever connection opens first does it, and no read sees a file in an
  older version than the code (a later migration would otherwise race the
  load's read). With version 1 only, the step is `CREATE TABLE IF NOT
  EXISTS`.
* **The main connection opens on first use**, all after the window is shown:
  the first play, cloud result or refresh (below).

| Operation | Statement |
| --- | --- |
| A song starts | `INSERT (key, 0, now) ON CONFLICT DO UPDATE SET last_played_timestamp = excluded.last_played_timestamp` |
| A play counts | `INSERT (key, 1, 0) ON CONFLICT DO UPDATE SET play_count = play_count + 1` (the cloud upload sends a delta of 1) |
| A cloud page | per item `INSERT (key, n, 0) ON CONFLICT DO UPDATE SET play_count = MAX(play_count, excluded.play_count)`, one transaction per page |
| `values(keys)` | one prepared `SELECT ... WHERE song_identity_key = ?` per key, in one read transaction |
| `allValues()` | `SELECT song_identity_key, play_count, last_played_timestamp FROM song_play_stats` |
| Rebase (local counts) | `SELECT song_identity_key, play_count FROM song_play_stats` |

* No statement reads a count from the host's copy, so a stale copy can never
  be written back.
* A `last_played_timestamp` of 0 (a row a cloud page made before any local
  play) is no value: the song shows a count and no last played time.
* The class is `PlayStatsStore`; the threads it uses are in "Threading
  contract".

### The identity key

* `normalize(title) | normalize(artist) | normalize(album)`, `normalize`
  being `trimmed().toLower()`, a missing field an empty string
  (`songIdentityKey` in `playstats.cpp`).
* **Kept as the cloud has it.** The cloud stores these keys; a different
  rule would leave every cloud row under an old key and need a re-key of
  DynamoDB as well as of the local file. Known limits, recorded in the
  roadmap:
  * `|` inside a tag can make two songs share a key ("A|B" by "C" and "A"
    by "B|C", no album).
  * `QString::toLower` follows Qt's Unicode tables, so a Qt upgrade can
    change a few keys.
  * Songs with the same three tags share stats, untagged ones too
    (`intro||`); that is the identity rule.
* Changing the rule later means a plugin schema version that re-keys the
  rows (merging the ones that collide) and a re-key of the cloud; the
  comment at `songIdentityKey` says so.

### The app's database

`myplayer.db` has no play stats and no identity: no `song_identities`, no
`song_play_stats`, no `songs.identity_id`. A database from before the plugin
is migrated once (see "Migration").

## Counting plays

### Events

`IPlayerListener` gains five events, each forwarded by `PluginPlayerHost`
from a `PlaybackController` signal:

| Event | From | Used for |
| --- | --- | --- |
| `durationChanged(ms)` | `durationChanged` | the thresholds |
| `paused()` | `playbackStateChanged(Paused)` | listened time counts only while playing |
| `resumed()` | `playbackStateChanged(Playing)` | the same |
| `seeked(ms)` | `seeking` (before the backend is told) | a seek adds no listened time |
| `finished()` | `songFinished` (before Next) | count at the end of the song |

* A core API change; versions stay at 1 (plugin-system.md, "Versions").
* A song start sends `trackStarted`, not `resumed`, as `PlaybackController`
  sends `songStarted` instead of `playbackStateChanged`.

### The rule

`PlayStats` (the plugin's listener) feeds `PlayCompletionTracker`: a play
counts once per session when the furthest position reaches
`min(90%, end - 5 s)` and two thirds of the duration were heard while
playing; a seek adds no listened time.

* **The session starts at `trackStarted`.** The plugin computes the song's
  key from `TrackInfo.fields` with its own `songKey` and keeps it for the
  session, so a play counts for the song that played even if its tags change
  while it plays.
* **The duration can arrive first.** The backend may report the duration
  during `setSource`, before `songStarted`; the tracker keeps the duration
  across sessions (`startSession` does not reset it).
* **Playing or not** follows the events: `trackStarted` and `resumed` set
  it, `paused` and `stopped` clear it.
* `trackStarted` writes `last_played_timestamp`; a counted play writes
  `play_count + 1`; each then calls `valuesChanged({key})`, as a cloud page
  does with its keys.
* A missing file is skipped before `songStarted`
  (`PlaybackController::playSong`), so it records nothing.

### Lifetime

The root object is one per process and never unloaded, while `initialize`
and `shutdown` run once per `PluginManager` (once per window; the tests make
many). So everything with a lifetime is created in `initialize` and
destroyed in `shutdown`, not in the root's constructor and destructor:

* the listener (added and removed), the cloud coordinator and its thread,
  and the main-thread connection (closed and removed in `shutdown`);
* the queued cloud start uses the coordinator as its timer's context, so a
  window destroyed before the event loop runs drops it instead of starting
  sync on a shut-down plugin;
* connection names are unique: the main one per `initialize`, one off the
  main thread per call, as `libraryloader.cpp` names the worker's.

**Every host call to a plugin happens between its `initialize` and its
`shutdown`**, the load's workers included: `~MainWindow` waits for a running
library load before its `PluginManager` (a child, destroyed after the
destructor's body) shuts the plugins down. Queued host work (a deferred
`valuesChanged`, the refresh) runs only from the event loop, which does not
run during the destruction, and its context objects die with the window. So
the provider's calls read the members `initialize` set (the host pointer,
the data folder) like any other call. `loadLibrary` returns the worker's
future for that wait. Rejected: letting the worker outlive the window (the
watcher only dropping its result): then `songKey` and `allValues` would be
called after `shutdown`, and while
the next window's `initialize` runs on the same root object, so they could
read no member and would take the data folder as an argument. Cost: closing
the window during the load waits for the rest of it (under a second for 50k
songs; the tests' delayed load is 300 ms).

### `PlaybackController`

* It does not count; it sends the events above.
* `playSong` refreshes the song's rows after its tag refresh
  (`refreshSongFromFile`), with `notifySongChanged(songPk)`: tags changed on
  disk and read at play repaint at once, with the plugin disabled too.

## Fields in the host

### The capability: `ISongFieldProvider`

```cpp
enum class SongFieldType { Number, DateTime };  // DateTime: epoch seconds

struct SongField {
  QString id;                 // lowercase, e.g. "play_count"
  QString title;              // column title, e.g. tr("Play Count")
  SongFieldType type;
  int defaultWidth;
  bool visibleByDefault;
};

// One value per field of songFields(), in that order; an invalid QVariant
// means no value. Both types are a qint64.
using SongFieldValues = QList<QVariant>;
// Values by key; a key with no entry has no values.
using SongFieldValuesByKey = QHash<QString, SongFieldValues>;

// Threads: songFields and keyFields on the main thread; songKey, values and
// allValues on the main thread or the library load's workers. All between
// initialize and shutdown.
class ISongFieldProvider {
public:
  virtual QList<SongField> songFields() const = 0;
  // The built-in tag fields the key is computed from, e.g. title, artist,
  // album.
  virtual QStringList keyFields() const = 0;
  // Songs with the same key share values. keyFieldValues holds the fields'
  // text in keyFields() order (an absent field is an empty string).
  // Deterministic and reentrant.
  virtual QString songKey(const QStringList &keyFieldValues) const = 0;
  // The current values of these keys.
  virtual SongFieldValuesByKey values(const QStringList &keys) const = 0;
  // The current values of every key that has any, for the startup load.
  virtual SongFieldValuesByKey allValues() const = 0;
};
```

* **One type enum, holding only what a plugin uses** (plugin-system.md, "The
  API holds only what its plugins use"); the host maps it to its
  `ValueType` and `DisplayKind` (`DateTime` to `EpochSecondsDateTime`). Text
  or Boolean is added when a plugin needs it. Rejected: a separate display
  enum, which for these types only restates the type.
* **Two reads, one result type, one thread rule.** `values(keys)` serves
  every refresh after the load, batched ("What `SongLibrary` keeps");
  `allValues()` serves the load, where the host does not yet know which keys
  have values and asking for each of 45k keys would cost far more than one
  scan of 15k rows. The host calls `values` on the main thread only,
  but both reads follow the same rule, so a plugin writes its reads one way
  (a connection of its own off the main thread). Rejected: a single-key
  read called per song added (a scan adding 5k files, one query each,
  inside the scan's loop), with a thread rule of its own.
* **Typed values in field order, key fields as a list.** The load converts
  each value once, from the `QVariant` SQLite returns, and builds no hash per
  song or per row. Rejected: values as `QHash<field id, text>` and key fields
  as `QHash<field id, text>`: at 50k songs and 15k rows, about 65k small
  hashes and a number-to-text-to-number round trip per value, on the path to
  showing the library, for no gain in clarity.
* **The key is opaque to the host.** It groups songs and names them in
  `valuesChanged`; only the plugin knows its rule. Rejected:
  * fields only, no key: a change could not say which songs it reaches (the
    plugin gets keys from the cloud, not songs), and the host could not know
    that "Song" and "song " share values;
  * song handles (the host's pks) with add, change and remove events: the
    plugin would mirror the library's song list;
  * the host computing identity and passing it: identity exists only for
    play stats, so its rule belongs to the plugin.
* **Declared key fields.** The host passes only those fields, and recomputes
  a key only when one of them changes. It passes each field's
  `FieldValue::text`, the text `TrackInfo.fields` carries
  (`pluginplayerhost.cpp`, `toTrackInfo`), so the key the plugin computes at
  `trackStarted` is the key the host files the song under. Rejected:
  passing every field (a million string conversions at load for 50k songs,
  the plugin's own fields in the input, and no way to know when to
  recompute).

### One `IPluginHost` per plugin

Some services belong to one plugin each (its data folder, its fields'
changes), and a host shared by every plugin could not tell which plugin
calls. So `PluginManager` gives each loaded plugin its own host object
(`PluginHost`, in `pluginmanager.cpp`), which knows the plugin's id and
forwards `player()` and `control()` to the shared ones. Rejected: passing the
plugin's id or
provider to each call: a plugin could pass another's, and every call would
repeat what the host already knows.

### The host service: `ISongFieldHost`

`IPluginHost::songFieldHost()` returns it; `valuesChanged(const QStringList
&keys)` reports that those keys of this plugin's provider changed.

* **Deferred, callable from any thread**, like commands: the host queues it
  (`QMetaObject::invokeMethod`, `Qt::QueuedConnection`, the host as context)
  and runs it on the main thread. The plugin calls it inside `trackStarted`,
  which runs inside `PlaybackController::playSong`; refreshing rows and
  autoplaylists there would nest inside starting a song
  ([event-ordering.md](../event-ordering.md), "Who queues each edge": a new
  plugin edge, queued by the host). Cost: rows repaint at the queued refresh
  ("What `SongLibrary` keeps"), not at once.
* **Keys only, never values.** The host reads the values when it refreshes,
  so it always gets the current ones, whatever order changes arrive in.
* On the main thread `PluginManager` emits
  `providedValuesChanged(pluginId, keys)`, which `MainWindow` connects to
  `SongLibrary::markStale` (below).

### `IPluginHost::dataFolder()`

The plugin's own folder: `<data root>/<id>/`. `MainWindow` passes the data
root to `PluginManager` beside the plugin folder: `<AppDataLocation>/plugins`
in the app, beside `myplayer.db`. The tests give a temporary folder:
`MainWindow`'s test constructor (database and connection name) gains a data
root parameter, and `tst_plugins` passes one to `PluginManager`, so no
plugin touches the developer's data. The host returns the path only; the
plugin creates the folder on its first write.

### Registration (`PluginManager` and `ColumnRegistry`)

At plugin load, for each provider:

* Each field goes into `ColumnRegistry` with a new source,
  `ColumnSource::Plugin`: a column and an expression symbol (name and
  resolved id both the field id, so `play_count` reads `song["play_count"]`,
  as custom fields do), in `FieldTypePool`; not a `songs` column, kept by
  `loadDynamicColumns`, not writable.
* A field id already used by a field, a column or an expression symbol
  (`ColumnRegistry::isNameInUse`), or by another provider, is refused and
  logged, and the provider is not used. Custom and computed fields load
  later, with the library, so the Fields page refuses a custom or computed
  key that is a plugin field's id instead (`isPluginFieldId`,
  `isReservedComputedFieldKey`): both would take the same expression symbol.
  One made while the plugin was disabled keeps its name; its symbol comes
  first, so expressions read it rather than the plugin's field.
* Key fields must be built-in tag fields (the `songs` columns); any other is
  refused the same way. `syncBuiltInFieldsBySongId` writes exactly those, so
  it is the one place a key can change. Custom tag fields are written after
  it (`syncDynamicAttributesBySongId`) and computed values change without
  the song being re-read; neither is needed now.
* This happens in the `MainWindow` constructor, before `NowPlayingPresenter`
  parses the display expressions, so `${play_count}` resolves.
* `PluginManager::registerSongFields` wraps the provider into a
  `ProvidedFields` (`providedfields.h`: the fields' ids and types, the key
  fields, and `songKey`, `values` and `allValues` as `std::function`s) and
  gives it to `SongLibrary::addProvidedSource`, so `SongLibrary` stays free
  of the plugin API.

### What `SongLibrary` keeps

Per source (one per provider, by plugin id):

```cpp
struct ProvidedKeys {  // in the load's snapshot too
  std::vector<QString> keyBySongPk;                          // "" for none
  std::unordered_map<QString, std::vector<int>> songsByKey;  // no empty lists
};
struct ProvidedSource {
  ProvidedFields provider;
  ProvidedKeys keys;
  QSet<QString> staleKeys;  // values to read at the next refresh
};
std::map<QString, ProvidedSource> providedSources_;
```

* Memory only; keys are recomputed at each start from the stored tag
  fields.
* The key is not an `MSong` field, so it does not appear in `TrackInfo`,
  Song Properties or expressions.

| Event | `SongLibrary` |
| --- | --- |
| Load (worker) | per song: `songKey(key fields)`, `keyBySongPk`, `songsByKey`; then the fill from `allValues()` |
| A song is added (`addTolibrary`, new path) | its key; the key marked stale |
| Tags re-read (`syncBuiltInFieldsBySongId`) | if a key field's text changed: the new key; move the song in `songsByKey`; remove its provided fields; the new key marked stale |
| `removeSongs` | remove the song from `songsByKey`, clear its key |
| `valuesChanged(keys)` | the keys marked stale |
| `refreshStale()` | `values(stale keys that songs have)` per source, replacing those songs' provided fields; returns their pks |

**The fill** walks the rows `allValues` returned (one per key with stats)
and looks each key up in `songsByKey`, skipping rows no song has, rather
than looking up every song's key in the rows, most of which have none. It
builds each value's `FieldValue` once per key, by the field's type from
`ProvidedFields` (`FieldValue`'s constructor that takes a type: the worker's
own `ColumnRegistry` has no plugin fields), and copies it into the key's
songs. A song with no values has no provided fields: an unplayed song has no
`play_count`.

**One path for every change after the load.** A song added, a key changed
by a re-read, and the plugin's `valuesChanged` all only mark keys stale
(`markStale`); `refreshStale` reads them with one `values(keys)` call per
source and returns the pks whose fields it replaced. When the library goes
from no stale keys to some, `markStale` calls a callback
(`setStaleCallback`), from which `MainWindow` queues one `refreshStale`
(`QTimer::singleShot(0, this, ...)`) and passes its pks to
`PlaylistCollection::notifySongsChanged`; `refreshStale` clears the state,
so the next change queues the next refresh.

* **Batched**: a scan adding 5k songs in one loop is one read, after the
  loop, not 5k inside it.
* **Current**: `refreshStale` resolves each key to its songs when it runs,
  so a song removed in the meantime is skipped, and keys no song has are not
  read.
* **No stale values shown**: a song whose key changed loses its provided
  fields at once, and gets the new key's at the refresh.
* **The load**: `loadLibrary` marks the library loading (`beginLoad`,
  Startup step 3); while it is, `refreshStale` reads nothing and keeps the
  stale keys.
  `replaceWithSnapshot` keeps them too (the snapshot carries keys, index and
  values, not stale keys), clears the mark and runs `refreshStale`; its pks
  need no notification, since the playlists are built after it (step 6).
  So a change during the load (a cloud page; plays cannot, since media
  requests are ignored until the load ends) is read after the snapshot
  replaces the library, instead of being applied to the old one and lost.
* Cost: a song added shows its provided fields one event-loop turn after
  the song; an autoplaylist on them takes it in at that refresh.
* Rejected: reading values synchronously in `addTolibrary` and
  `syncBuiltInFieldsBySongId`: one read per song inside the callers' loops,
  and a second path beside `valuesChanged` with its own rules for the load.

**A re-tag starts fresh**: the song shows the new key's stats, or none, and
the old key's row is kept, so tagging it back restores them. Rejected:
carrying the old stats over. Stats are per identity, not per file, so when
other files keep the old tags, a copy would count the same plays twice, and
a move would take plays that may be theirs.

### Startup

| # | Step | Thread |
| --- | --- | --- |
| 1 | `PluginManager` loads the plugins; the play stats plugin's `initialize` stores the host, creates its objects, adds its listener, and queues its cloud start (`QTimer::singleShot(0, ...)`) | main, no I/O |
| 2 | Registration (above) | main |
| 3 | `loadLibrary` marks the library loading and starts the worker job with the `ProvidedFields` | main |
| 4a | Each provider's `allValues`, in a task of its own | worker 2 |
| 4b | Songs loaded; keys and index; wait for 4a; the fill; the snapshot moved out | worker |
| 5 | `replaceWithSnapshot`: songs arrive with keys, index and values; the loading mark cleared; `refreshStale` | main |
| 6 | `finishSetUpPlaylist`: playlists and autoplaylists see `play_count` | main |

* **The plugin's read runs beside the songs' load** (4a), since it needs
  nothing from them, so the second file costs the load only the fill. The
  worker waits on that future; a task not started yet runs inline in the
  waiting thread (`QFuture::waitForFinished`), so a one-thread pool cannot
  deadlock.
* **The snapshot is moved, not copied.** `SongLibrary::takeSnapshot()`
  moves the songs, paths and keys out of the worker's library, which is
  discarded right after; a copy would duplicate every field of every song,
  likely more than the plugin adds.
* **`songKey` and `allValues` run on worker threads**, an exception to
  "all calls are on the main thread" (plugin-system.md, "Rules") that the
  header gives `values` too ("Two reads, one result type, one thread rule"),
  so the load costs the UI thread nothing for the plugin. `songKey` is
  reentrant and the reads use a connection of their own there; all read
  only members that `initialize` set before the load started, and run
  before `shutdown` ("Lifetime"). Rejected:
  * main thread only: the keys and the fill (15–60 ms, "Costs") on the UI
    thread at every start, for 50k songs;
  * an in-memory store read in `initialize` and copied for the worker: about
    15 ms on the main thread before the window shows;
  * the host storing each song's key in its own database: plugin data in the
    app's schema, stale when the plugin's rule changes.
* The `:memory:` path (tests) runs the same steps synchronously.

### Where the fields show

* **Columns, sorting, Playlist Search, autoplaylists, display
  expressions**: they read `MSong`, as for any field. Library Search's
  results show plugin columns like song attribute columns
  (`LibrarySearchResultsModel`).
* **Properties tab** (`buildSongInfo`): the Library section lists the
  plugin fields (`ColumnRegistry::pluginFieldIds`), in registration order.
* **Song Properties**: plugin fields are read-only by their source
  (`ColumnSource::Plugin`).
* **New Autoplaylist's placeholder** is an example on built-in fields
  (`genre HAS jazz AND tracknumber <= 3`), valid without the plugin.
* **Computed fields** are evaluated on the freshly parsed song
  (`prepareParsedSong`), which has no plugin values, so one that uses
  `play_count` gets no value from it. With the plugin disabled such an
  expression fails to parse and the field has no value
  (`evaluateComputedFieldsInSong`), with no other effect.

## Cloud sync in the plugin

* `CloudPlayStatsSyncCoordinator` and `CloudPlayStatsSync` are the
  plugin's, with their worker `QThread`; the plugin links `Qt6::Network` and
  `Qt6::Sql`, the app neither for this. The design is
  [cloud-sync.md](../cloud-sync.md)'s; its local side reads and writes the
  plugin's file (`PlayStatsStore`): a page is one `mergeCloudCounts`, then
  `valuesChanged` with its keys, and the rebase's local counts are a
  `SELECT`.
* **Started after the window shows**: `initialize` queues the start with a
  zero-delay timer, which runs at the event loop's first turn, after
  `w.show()`. The sync does not wait for the library load: it writes only
  the plugin's file, and its `valuesChanged` reaches the songs after the
  load ("What `SongLibrary` keeps", "The load").
* **Settings**: the plugin's `ISettingsPageProvider` page, Play Stats under
  Plugins (`PlayStatsSettingsPage`). Its keys are in the plugin's area:
  `playstats/user_uuid`, `playstats/disabled_by_user`,
  `playstats/last_synced_at`, `playstats/rebase_pending`
  (`CloudSyncSettings`). The UUID field is read-only and a UUID is entered
  only through the page's dialogs, which refuse an invalid one, so `apply()`
  has nothing to refuse (an invalid stored value is kept and sync skips it).
  When the UUID changed, the page sends `uuidChanged` and the plugin starts
  a rebase.
* **Manual rebase** is in a Play Stats submenu at the end of Playback
  (`IMenuProvider`), the only menu plugins add to. It is enabled while the
  UUID is valid, checked when the menu opens (as A-B Repeat's items); it
  does not wait for the library load, which the sync does not need.
  Rejected: a new capability for the Library menu, for one item.

## With the plugin disabled

* Its fields are not registered: the Play Count and Last Played columns are
  gone, Library Search and the Properties tab show no stats.
* An autoplaylist or display expression using `play_count` fails to parse
  and shows its existing error until the plugin is enabled.
* The column layout drops the fields' place: on re-enabling they come back
  at the end (`GlobalColumnLayoutManager::refreshFromRegistry`), as a
  removed custom field's would.
* Nothing is counted or synced; the plugin's file is kept.

## Threading contract

| Call | Direction | Thread |
| --- | --- | --- |
| `songFields`, `keyFields` | host → plugin | main |
| `songKey`, `values`, `allValues` | host → plugin | main or the library load's workers (new exceptions, documented in the header) |
| New listener events | host → plugin | main, synchronous |
| `valuesChanged` | plugin → host | any, deferred |
| `dataFolder`, `songFieldHost` | plugin → host | main |

Every call in the table happens between the plugin's `initialize` and its
`shutdown` ("Lifetime"). plugin-system.md's "Rules" list the exceptions,
that lifetime rule, and **no I/O while plugins load**. The
plugin's `initialize`, `shutdown` and the capability calls made while the
window is built (`createPanel`, `createMenu`, `songFields`, `keyFields`) do
no file, database or network I/O; reading QSettings is allowed (QSettings
reads its store once). Loading the library files themselves is the host's,
and is unchanged. Work that needs I/O starts later: on a listener event,
from the event loop, on the plugin's own thread, or in the provider's
reads, which the host makes only from the library load's workers or the
event loop. The existing plugins already follow it.

## Migration

A one-off script, run once on copies, not kept in the app:

1. Copy the stats into the plugin's file:
   `ATTACH` both databases, create the plugin's table, `INSERT INTO
   song_play_stats SELECT i.song_identity_key, s.play_count,
   s.last_played_timestamp FROM song_play_stats s JOIN song_identities i
   USING (identity_id)`, set `user_version = 1`.
2. Rebuild the app's `songs` without `identity_id`, with foreign keys off:
   create the new table with the current columns, copy, drop the old one,
   rename; then restore `songs`' `sqlite_sequence` value, so removed song ids
   are never reused; drop `song_identities` and `song_play_stats`; run
   `PRAGMA foreign_key_check`. SQLite refuses `DROP COLUMN` on a column in a
   foreign key, hence the rebuild.
3. Copy the `cloud_sync/*` settings to `playstats/*` and remove the old
   keys (on Windows they are in the registry, under
   `HKCU\Software\Example\myplayer`). The UUID is copied, never printed.

The keys are unchanged, so the cloud needs nothing. Rejected: deleting
`myplayer.db`: it would lose playlists, watched folders,
custom fields and every last played time.

## Rejected

* **The plugin writing tables in the app's database**: it breaks the
  boundary, couples the two schemas and puts two writers on one file.
* **A host storage service** (a key-value API) instead of the plugin's own
  SQLite: more host API for what QtSql gives the plugin.
* **The host reading values from the plugin on each use**: sorting and
  autoplaylists would cross the boundary per comparison; values live in
  `MSong`.
* **Keeping the stats in the app and moving only cloud sync**: the stats
  would stay in the app's schema and `PlaybackController`.
* **Integer ids in the plugin's table**, see "The plugin's database".

## Costs

* A larger plugin API: `ISongFieldProvider`, `ISongFieldHost`,
  `dataFolder`, five listener events, three calls also made off the main
  thread, whose rules the host cannot enforce.
* A host copy of the values, current as of the last refresh.
* Disabling the plugin breaks expressions that use its fields, and its
  columns lose their place.
* Startup (estimates for 50k songs, 45k identities, 15k rows): about 15–60
  ms on the load's path (10–40 ms of keys, 5–20 ms of fill), the plugin's
  read hidden beside the songs' load; none on the UI thread. Moving the
  snapshot instead of copying it likely saves more than that. Not measured.
* Closing the window during the load waits for the load to finish.
* Two databases, so no transaction spans stats and songs; nothing links
  them.
* Each song added (Open, Add Folder, a scan's new files) costs a key, and
  its batch one `values` read on the main thread at the next event-loop
  turn (a primary-key lookup per key, about 5–20 µs each). Its provided
  fields show one turn after the song.
* `IPluginHost` is one object per plugin.
* The plugin's `shutdown` stops the cloud thread
  (`~CloudPlayStatsSyncCoordinator`: a blocking queued call to the worker,
  then `quit` and `wait`). How long it waits when the worker is inside a
  request or a throttle retry (both run nested event loops, `waitForReply`
  and `waitForDelay`) is not checked; the dummy cloud the tests use has no
  retries.
* Manual rebase is in the Playback menu, not Library.
* A one-off migration of the database and the settings.

## Tests

* **Host**: `tst_songlibrary` (keys and the fill at load; a batch of adds is
  one `values` call; a song removed before the refresh and keys no song has
  are not read; a re-tag drops the old key's values at once, reads the new
  key's, and an unchanged re-read reads nothing; a change marked during a
  load is read after `replaceWithSnapshot`; a provided date field in
  expressions), `tst_libraryloader` (the values filled at load, read off the
  main thread for a file database), `tst_columnregistry` (plugin fields as
  symbols, kept by `loadDynamicColumns`, reserving their names),
  `tst_columnlayout` (a column registered after the layout loads keeps its
  saved width), `tst_plugins` (the new listener events; the fields
  registered, a provider whose id clashes refused; `valuesChanged` deferred;
  the plugin's file under the data root the test gives, none created while
  loading), `tst_autoplaylist` and `tst_songinfo` (a refresh reaches
  autoplaylists and the Properties tab).
* **Plugin** (`tst_playstats`, linking its static library `myplaystats`):
  the key rule; the store on a temporary file, read from another thread,
  with and without a file (a missing file is not created); counting through
  listener events (pause, seek, finish, duration before the start or only at
  the end); the plugin loaded from `Tests/plugins`, initialized and shut
  down twice on one root object, `initialize` creating no file, and a
  shutdown before the event loop runs dropping the queued sync.
  `tst_playcompletiontracker`, `tst_cloudplaystatssyncservice` and
  `tst_cloudplaystatssynccoordinator` (on the store) link the same library.
  No test reaches the network (`MYPLAYER_USE_DUMMY_CLOUD_SYNC`): the static
  library is built with `MYPLAYER_TESTING` in test builds, as `MYLib` is
  (`src/Tests/CMakeLists.txt`), so the plugin `tst_mainwindow` loads has the
  dummy too; its state is the plugin's copy, which the tests cannot set, so
  the cloud's flows are tested on the coordinator.
* **App** (`tst_mainwindow`): a counted play shows in its row and fills an
  autoplaylist on `play_count`; with the plugin disabled, no Play Count
  column and no submenu; a window closed during a delayed load
  (`MYPLAYER_TEST_LIBRARY_LOAD_DELAY_MS`) waits for it.
* By hand: the Play Stats page and submenu, a sync with a real UUID, and
  re-enabling the plugin.
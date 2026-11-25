# Monitored Library, Autoplaylists and Playlist Search

## Summary

* **Monitored library**: the user picks folders. The app keeps the audio
  files under them in the library automatically: new files are added,
  changed files are re-read, deleted files leave.
* **Autoplaylists**: playlists defined by a library expression. Their rows
  are the monitored songs that match it, kept up to date as songs change.
* **Playlist Search**: a search panel over the playlist shown in the UI.
  Library Search covers the monitored library only.

Manual playlists, filled with Open / Add Folder, are the **manual library**
below. The two libraries never change each other's membership but share one
`songs` row per file path, so a file in a watched folder and also added by
hand is one song with one set of tags and plugin values (play stats).

The filters (Library Search, autoplaylists, Playlist Search's expression
mode) are conditions: `parseSongFilter` ([expressions.md](expressions.md)).

## Core model

### Terms

* **Song row**: one row in `songs`, one per canonical file path, with a
  `song_id` (pk). It holds tags, `attr:` and computed values; play stats are
  the Play Stats plugin's, by a key of the song's tags
  ([plugins/playstats.md](plugins/playstats.md)). In memory the pk is the
  index into `SongLibrary::songs`.
* **Owner**: a library that keeps song rows alive. There are exactly two.
  * **Manual library**: songs in at least one manual playlist
    (`playlist_items`).
  * **Monitored library**: songs recorded in `monitored_files`. A file is
    recorded there when a scan finds it under a watched folder and TagLib can
    read it, and stays recorded until a scan of an **online** folder no
    longer finds it (or can no longer read it), or its folder is removed.
* **Watched folder**: a directory in `library_folders`, scanned recursively.
  It is **online** or **offline** (see "Offline folders"). Watched folders
  can't be nested or overlap: adding one inside, or containing, a watched
  folder is rejected with a message (compared by path component,
  case-insensitively on macOS and Windows).
* **Autoplaylist**: a playlist with `kind = 'auto'` and an expression. Its
  rows are computed; only their order is stored (`autoplaylist_order`). It is
  not an owner.

### Invariants

1. **One row per path.** `SongLibrary::addTolibrary`, which every add goes
   through (Open, Add Folder, the scanner), rewrites `filepath` to
   `QFileInfo::canonicalFilePath` (or the cleaned absolute path if the file
   doesn't exist) before the path lookup. Symlinks and `..` never create a
   second row.
2. **Independent membership.** Manual operations only write `playlist_items`;
   monitored operations only write `library_folders` / `monitored_files`.
   The only place that looks at both is the release check (invariant 3).
3. **Song lifetime.** When either library lets go of songs, each one is
   deleted if and only if the other library does not hold it:
   `delete(pk) <=> pk not in playlist_items and pk not in monitored_files`.
4. **One delete path.** All deletes go through
   `PlaylistCollection::releaseSongs(pks)`, which asks
   `MonitoredLibrary::holds`. It is the only caller of
   `SongLibrary::removeSongs` and the only emitter of
   `songsRemovedFromLibrary`.
5. **Views first, then delete.** A song leaves every view (playlist rows,
   autoplaylist rows) before `releaseSongs` deletes it, so no view ever
   holds a deleted pk. A manual removal removes the rows, then sends
   `songPksRemoved`; for monitored songs, `PlaylistCollection` handles
   `songsLeft` by first removing autoplaylist rows, then calling
   `releaseSongs`.
6. **Autoplaylists own nothing.** They never write `playlist_items`, never
   cause a release, and removing one deletes only its `playlists` row (its
   `autoplaylist_order` rows cascade). `autoplaylist_order` records row order
   only and is not part of the release check.
7. **Each monitored file belongs to exactly one watched folder**
   (`monitored_files.folder_id`), because folders don't overlap and the walk
   doesn't follow symlinks.
8. **Song data is shared.** Tag edits, metadata refresh and a plugin's
   provided values (play stats) write the shared row, and every view of that
   pk refreshes
   (`PlaylistCollection::notifySongChanged`), whichever library the change
   came through.

### Membership per library

| | Manual library | Monitored library |
|---|---|---|
| Stored in | `playlist_items` | `monitored_files` |
| Grain | (playlist, song); a song can be in many playlists | one row per song |
| Joins by | Open, Add Folder, Create playlist from Library Search | a scan finds a readable file under an online watched folder |
| Leaves by | Remove from Playlist, Clear Playlist, Remove Tab; released when the song is in no manual playlist | a scan of an online folder no longer finds the file (deleted, moved out, renamed to an unsupported extension) or can't read it any more, or the folder is removed |
| File missing on disk | row kept with its last tags, skipped on play (there is no "missing" marker) | leaves on the next scan, unless its folder is offline (then kept, skipped on play) |
| Appears in autoplaylists | no | yes, when the expression matches |

### Events and transitions

`M` = held by the manual library, `W` = held by the monitored library.
Signals are listed in the order they are emitted.

| Event | Before | After | Effects |
|---|---|---|---|
| Scan finds a new file | - | W | create row, insert `monitored_files`; `songsEntered` |
| Scan finds a file that already has a row | M | M+W | reuse pk, refresh tags from the file, insert `monitored_files`; `songsEntered`, `notifySongChanged` |
| Missing file reappears under a watched folder | M (missing) | M+W | same as the row above |
| Manual add of a monitored file | W | M+W | reuse pk, append to the playlist; monitored library unchanged |
| File changed (mtime or size differ), still readable | W or M+W | same | re-parse, update row and stored stats; `songsUpdated`, `notifySongChanged` |
| File changed and now unreadable | W or M+W | - or M | treated as gone (rows below), path goes into the failed set |
| File gone from an online folder | W | - | delete `monitored_files` row; `songsLeft` (autoplaylist rows removed), then `releaseSongs`: row deleted, `songsRemovedFromLibrary` |
| same | M+W | M | delete `monitored_files` row; `songsLeft`, then `releaseSongs`: row kept (still in manual playlists) |
| File moved (within or between watched folders) | W at A | W at B | gone at A + new at B: B gets a new pk, play stats carry over by key (same tags); a manual playlist holding A keeps A as a missing file |
| Folder removed from the watch list | W or M+W | - or M | delete `library_folders` row (cascades `monitored_files`), remove the watch; `songsLeft`, then `releaseSongs` for all its pks |
| Folder offline at scan | W or M+W | same | nothing released; `folderAvailabilityChanged(id, false)` |
| Offline folder available again | W or M+W | per scan | `folderAvailabilityChanged(id, true)`, add the watch, then a normal scan of it |
| "Remove missing songs" on a folder offline because empty | W or M+W | - or M | scan that skips the availability check: every file counts as gone |
| App writes tags (Song Properties save) | W or M+W | same | refresh row, store new file stats; the watcher event that follows finds no change |
| Last manual reference removed | M+W | W | delete `playlist_items` row, `releaseSongs`: row kept, still in autoplaylists |
| same | M | - | `releaseSongs`: row deleted |
| New file unreadable by TagLib | - | - | not added; kept in the failed set (see "Unreadable files") |

A deleted and re-added file gets a new pk (`song_id` is `AUTOINCREMENT`), and
its play count comes back through its key (the plugin keeps rows of songs
that are gone).

## Change detection

### Scan

**When:** at startup, on "Rescan now", when a folder is added, for paths the
watcher reports, when an offline folder comes back, and for "Remove missing
songs".

**Scope:** a whole watched folder, or a path under one (a file or a
directory subtree, from the watcher). Only snapshot entries inside the scope
can count as gone.

**Scheduling:**

* One scan runs at a time, on a worker thread (`QtConcurrent::run` with a
  `QFutureWatcher`, like `loadLibrary`).
* Requests that arrive while a scan runs are queued and merged into one
  follow-up scan: a full-folder request covers any path requests inside that
  folder.
* Removing a folder doesn't cancel the running scan; when its results are
  applied, entries for folders no longer watched are dropped.

**Steps:**

1. **Snapshot** (main thread): for the scope, the stored
   `{canonical path -> (pk, mtime, size)}`, the failed set, and a copy of
   `ColumnRegistry` (a plain value class; the main thread may change the live
   one, for example in `SettingsDialog::customFieldsChanged`).
2. **Availability check** (worker): decide per watched folder in scope
   whether it is online (see "Offline folders"). This always looks at the
   folder root, even for a path scope. Offline folders are left out of the
   rest of the scan, so none of their files can count as gone.
3. **Walk** (worker): recursively list regular files whose suffix
   (case-insensitive) is a supported audio extension, without following
   symlinks and skipping hidden files and directories (names starting with
   `.`, which includes macOS `._*` files), and `stat` each one.
   Metadata only. The extension list is in a backend header
   (`audiofileformats.h`), shared with Open and Add Folder.
4. **Classify** (worker):

   | On disk | Snapshot | Result |
   |---|---|---|
   | yes | absent, not in the failed set with the same mtime and size | new: parse |
   | yes | absent, in the failed set with the same mtime and size | skip |
   | yes | present, mtime or size differs | changed: parse |
   | yes | present, same mtime and size | unchanged: skip |
   | no | present | gone |

5. **Parse** (worker): new and changed files with `SongParser::parse` and the
   registry copy. A failure adds the path to the failed set; for a changed
   file it also turns the entry into gone.
6. **Apply** (main thread), in batches of a few hundred per event-loop turn,
   each batch in one transaction, so the UI stays responsive:
   * Drop entries of folders no longer watched.
   * Before applying an entry, check that the stored stats for the path still
     equal the snapshot. If not (for example the app saved tags in the
     meantime), skip the entry and queue the path for another scan, so an
     older parse never overwrites newer data.
   * Fill computed fields with the live registry
     (`SongLibrary::prepareParsedSong`), then `addTolibrary`.
   * new -> insert `monitored_files`, `songsEntered`;
     changed -> update stats, `songsUpdated` + `notifySongChanged`;
     gone -> delete the `monitored_files` row, `songsLeft` (then
     `PlaylistCollection` releases, see invariant 5).

**Composed paths (macOS):** the walk converts the paths it lists to
composed Unicode (NFC). macOS can store a name decomposed (NFD; `ガ` as
`カ` + combining `゛`), and `std::filesystem` returns it as stored, while
`SongLibrary` keeps the NFC path Qt gives (`canonicalFilePath`); both open
the same file. Otherwise the stored path never matches the one on disk, and
each full scan drops such songs and the next adds them back. The watcher
needs no conversion: efsw's FSEvents paths are already NFC.

**Stored stats:** `mtime` is `std::filesystem::last_write_time` converted to
nanoseconds since its epoch, compared for exact equality, so coarse
timestamps (FAT, network drives) still work. `size` is in bytes.

**Why not checksums:** hashing every file reads the whole library at each
startup, and hashing only the header misses tags at the end of the file
(ID3v1, APE) and FLAC tag edits that reuse padding without changing the size.
The rare case mtime + size misses (a file restored with an old mtime and the
same size) is fixed by editing the file or with "Rescan now".

### Watcher

The watcher (efsw 1.7.3, `third_party/efsw`) is only a hint: it never
changes the library, it only requests scans, and the scan is the source of
truth.

```
efsw thread --handleFileAction(dir, file, action)--> pathsChanged (queued to main thread)
                                                         |
                       MonitoredLibrary: add to the pending set, restart a 1 s timer
                                                         |
                                timer fires --> scan request for the pending paths
efsw thread --handleMissedFileActions(dir)--> folderNeedsRescan (queued) --> scan request for the folder
```

* `EfswFileSystemMonitor` only forwards events to the main thread, as
  absolute paths (`dir` + `filename`; for a move both the old and new path).
  Debouncing is in `MonitoredLibrary`, so it is tested with
  `DummyFileSystemMonitor`.
* The action type (add, modify, delete, move) is ignored. An event only marks
  a path dirty; the scan works out what happened. A burst of events (a copy,
  a tagger saving several times) becomes one scan.
* A path is mapped to its watched folder by path-component prefix; paths
  under no watched folder are ignored. A path that is a directory, or no
  longer exists, is scanned as a subtree (for example a renamed or deleted
  album folder).
* Every scan runs the availability check first, so unmounting a drive while
  the app runs marks the folder offline instead of removing its songs.
* A file still being copied: the debounce usually waits it out. If a parse
  still sees a half-written file, it either fails (failed set, retried when
  the size changes) or reads partial data (re-parsed when the next write
  changes the size).
* If efsw can't add a watch (for example the inotify watch limit on Linux),
  the folder still works through scans; the Library page shows "Live updates
  unavailable" for it (`Folder::liveUpdatesUnavailable`). A watch that fails
  because the root is missing is not marked: the next scan marks the folder
  offline. The watch is tried again when the folder is added, at startup, and
  when the folder comes back online.
* Rejected: `QFileSystemWatcher`. It is not recursive: each directory needs
  a watch of its own, added for every new subfolder as it appears, so a
  library of thousands of folders holds thousands of watches against the
  system's limits. efsw watches a folder tree with one recursive watch.

### Offline folders

A watched folder is **offline** if, at scan time:

1. its root does not exist or can't be read (an unmounted drive on macOS and
   Windows, where the mount point disappears), or
2. its root is an empty directory (no entries at all) and the library has
   files for it (an unmounted Linux mount point is an empty directory).

A folder whose files were deleted but which still has subfolders or other
files is online, and its missing files leave normally.

While offline:

* Nothing is released. Its songs stay in the library and in autoplaylists,
  and Library Search still finds them. Playing one hits the missing-file
  skip.
* The state is in memory only and re-evaluated by every scan.
* It has no watch. `MonitoredLibrary` checks offline roots every 60 s (one
  `stat` or directory listing each, `checkOfflineFolders()`); when one is
  available again, it is marked online, its watch is added, and it is
  scanned. The timer runs only while a folder is offline.
* The Settings Library page shows the folder as "Offline", and after a scan
  the status bar shows how many folders are offline until the next scan.
* In case 2, the Library page offers "Remove missing songs" for the folder,
  for when the files really were deleted. In case 1, removing the folder from
  the list does the same.

### Own writes

Song Properties writes tags with `SongParser::writeTags`, which changes the
file's mtime and size and fires the watcher.

* `SongPropertiesDialog` calls `SongLibrary::refreshSongFromFile`
  after `writeTags` and then emits `songUpdated(pk)`. `openSongProperties`
  (`songdialogs.cpp`) connects that signal to
  `MonitoredLibrary::noteSongWritten(pk)` (reached through
  `PlaylistCollection::monitoredLibrary()`) as well as `notifySongChanged`.
  If the pk is monitored, it `stat`s the file and stores the new stats in
  memory and in `monitored_files`; otherwise it does nothing.
* The watcher event that follows (queued, then debounced) finds the file
  unchanged and skips it. A scan already running when the save happened
  fails the stale-stats check at apply and re-queues the path.
* Nothing can be lost: if the stored stats don't match the file at the next
  scan, the file is simply re-parsed.
* Playback and opening Song Properties also re-read tags
  (`refreshSongFromFile`) without touching stored stats. That is harmless:
  at worst the next scan re-parses a file whose tags are already current.

### Unreadable files

* A file TagLib can't open is not added. Its path, mtime and size go into an
  in-memory failed set, so later scans skip it until it changes.
* The set starts empty at each launch, so unreadable files are tried once per
  startup. This costs little (failed opens are quick, and the scan runs in the
  background), and files a newer TagLib can read show up without any manual
  step.

## Autoplaylists

### Membership

* Rule: `rows(ap) = { pk in MonitoredLibrary::songPks() | eval(ap.expression, song(pk)) is true }`.
  Evaluation uses the library-search context (`SongLibraryExprEvalContext`),
  in memory. The expression must be a condition (`parseSongFilter`). There
  is no "whole monitored library" view: an autoplaylist of `true` is one.
* Re-evaluated for:
  * the pks in `songsEntered` and `songsLeft`;
  * the pk in every `PlaylistCollection::notifySongChanged(pk)`: file
    changes, Song Properties, and `notifySongsChanged` for the songs a
    plugin's values reached (play stats). So `play_count > 10` gains a song
    as soon as its count passes 10;
  * the pks of a "Refresh Playlist Metadata" run: `Playlist` emits
    `songsRefreshed(pks)` after `songsReset`, which `PlaylistCollection`
    handles like `notifySongChanged` for each pk, so the same songs in other
    playlists refresh too;
  * all monitored pks when the expression is edited, and when custom fields
    change: `MainWindow`'s `customFieldsChanged` handler calls
    `PlaylistCollection::reloadAutoPlaylistExpressions()`, which re-parses
    every expression first.
* A plugin's values are per key: a change reaches every song sharing the key
  (`SongLibrary::refreshStale` returns their pks, which `MainWindow` passes
  to `notifySongsChanged`), not only the playing one, so autoplaylists on
  `play_count` stay right for duplicates.
* Songs in offline folders stay members (they are still in the monitored
  library).

### Row order

* A new autoplaylist starts in `filepath` order.
* The order is saved in `autoplaylist_order` (one row per current row, by
  `position`) and restored on load. Column-header sort
  (`Playlist::sortByField`) and dragging rows (`Playlist::moveRows`) reorder
  the rows and save the order, as for manual playlists.
* Refills (load, an expression edit, `reloadAutoPlaylistExpressions`) keep the
  order of the songs that still match; songs not yet in the playlist are
  appended in `filepath` order.
* Songs that start matching are appended at the end.
* A song that stops matching loses its saved place: if it matches again, it
  is appended. So an expression that becomes invalid (no rows) discards the
  saved order.

### Invalid expression

A parse error, a non-boolean expression, or a field that no longer exists:
the autoplaylist has no rows, and its tab shows a warning icon with the error
as a tooltip. The expression text is kept so the user can fix it. Loading
never fails because of an invalid expression; the playlist is just empty.

### Playback

* `last_played` (the resume pointer, a pk) is saved as for manual playlists.
* If the playing song stops matching (or leaves the monitored library while
  its row survives), it keeps playing. Next/Prev treat it as "current song
  not in the playlist", as for removed songs, and queued entries for rows
  that left are skipped (`PlaybackManager::next` checks `containsPk`).
* If the song row is deleted, `songsRemovedFromLibrary` stops playback.

### Operations by playlist kind

| Operation | Manual | Auto |
|---|---|---|
| File > Open / Add Folder (into the current playlist) | yes | disabled |
| Remove from Playlist / Clear Playlist (context menu and File menu) | yes | hidden in the context menu, disabled in the File menu |
| Column sort, dragging rows | yes (saved) | yes (saved) |
| Rename / reorder / remove tab | yes | yes; removing releases no songs |
| Properties / Open Containing Folder / Refresh Playlist Metadata | yes | yes (writes the shared row) |
| Play Next / Add to Playback Queue / play / follows-cursor | yes | yes |
| Edit Autoplaylist… (name and expression) | - | yes |

The last playlist can't be removed, of either kind.

`Playlist` refuses the write operations for an auto playlist (`addFiles`
returns no pks, `addSong` / `addSongByPk` return false, `removeSongs` and
`clear` do nothing, each with a `qWarning`). The UI never offers them.

## Search

| | Library Search | Playlist Search |
|---|---|---|
| Scope | monitored library | rows of the playlist shown in the UI, manual or auto |
| Input | boolean library expression | plain text or boolean library expression |
| Results | songs, in `filepath` order | rows, in the playlist's current order |
| Actions | Create playlist (manual), Save as autoplaylist | Play, Show in playlist, Add to Playback Queue, Properties |
| Opens from | `Library > Search` | `Edit > Search Playlist…` (`QKeySequence::Find`: `Ctrl+F`, `Cmd+F` on macOS; the only Edit menu item) |

### Library Search

* `SongLibrary::search(expr, candidatePks)` takes the candidate set and
  returns the matches in `filepath` order. `LibrarySearchDialog` passes
  `MonitoredLibrary::songPks()`.
* It uses the shared boolean check, so `davis` shows "Expression must be a
  condition" instead of a meaningless result.
* With no watched folders, the dialog shows "No watched folders. Add one in
  Settings > Library." instead of an empty result.
* "Create playlist" creates a manual playlist from the results, so
  those songs become `M+W`.
* "Save as autoplaylist" opens `AutoPlaylistDialog` with the expression
  filled in. Both use the same scope, so the autoplaylist shows exactly the
  songs the search showed.

### Playlist Search

* `PlaylistSearchDialog`: non-modal, one at a time. A query box, a match
  count, a mode indicator ("Text" / "Expression"), and a results table with
  the playlist's song columns (`LibrarySearchResultsModel` over
  `GlobalColumnLayoutManager`, so no Status column). An empty query shows no
  results.
* **Target**: the playlist shown in the UI (`PlaylistTabs::currentPlaylist()`),
  not the playing one. The title ("Search Playlist: <name>") shows its name.
  * Switching tabs re-targets the panel and re-runs the query, on
    `PlaylistTabs::currentPlaylistChanged(Playlist *)`.
  * Starting playback in another playlist does not.
  * If the target playlist is removed, the tab shown next becomes the target
    (through the same signal).
* **Mode**, decided on every edit (results of the current parser, checked):
  * **Expression** if the text parses and its static type is boolean.
    Examples: `genre HAS jazz AND title != x`, `true`, a boolean custom
    field.
  * **Plain text** otherwise, and always when the whole text is in double
    quotes with none inside (the quotes are removed). The text is split on
    whitespace, and a row matches when every word is a case-insensitive
    substring of the displayed value (`FieldValue::display()`) of at least
    one visible column. Examples: `davis blue` and `title` (both parse as text, not
    boolean), `simon and garfunkel` (fails to parse: "AND operands must be
    boolean"), `ac/dc`.
  * **Error**: if the text fails to parse and contains an expression symbol
    (`=`, `<`, `>`, `(`, `)`, `[`, `]`, or a backtick; this covers `!=`,
    `<=`, `>=`), the panel shows the parse error and no results, so a typo in
    an expression (`genre IN [pop`) doesn't silently become a text search.
    Keywords alone (`and`, `or`, `not`, `has`, `in`) don't trigger this,
    because they are common words and the tokenizer matches them
    case-insensitively.
* **Updates**: results update as you type (debounced about 150 ms), and
  whenever the target playlist inserts, removes, resets, reorders or changes
  rows, or the visible columns change (debounced), so an autoplaylist
  changing underneath stays correct.
  The selected result stays selected while its song still matches.
* **Actions on a result row** (each looks up the row by pk at action time,
  because the result list can lag the playlist by the debounce; a song no
  longer in the playlist does nothing):
  * Double-click or Enter: play the row (`PlaybackController::playRow`, which
    plays from the shown playlist, the same one) and select it in the
    playlist. Enter is handled by the dialog, since item views don't emit
    `activated` for it on macOS.
  * "Show in Playlist" button: select and scroll to the row
    (`PlaylistTabs::navigateIndex`) without playing.
  * "Add to Playback Queue", "Properties" buttons: as in the playlist's row
    context menu.
* **Backend split**: `matchPlaylistRows(const Playlist &, const SongMatcher &)`
  returns the pks of the matching rows, in row order. `SongMatcher` is built
  from the query text, the `SongLibrary` (for parsing) and the visible field
  ids (plain strings), so it has no UI types and is tested without a view.

## Schema

Created by `DatabaseManager`, which sets `PRAGMA foreign_keys = ON` for the
cascades. No schema migrations: tables are created with `CREATE TABLE IF NOT
EXISTS`, so a new table reaches an existing database; cost: changing an
existing table needs a fresh database. The monitored library's tables, and
`playlists` with `kind` and `expression`:

```sql
CREATE TABLE IF NOT EXISTS library_folders (
  folder_id INTEGER PRIMARY KEY AUTOINCREMENT,
  path      TEXT NOT NULL UNIQUE                -- canonical path
);

CREATE TABLE IF NOT EXISTS monitored_files (
  song_id   INTEGER PRIMARY KEY REFERENCES songs(song_id) ON DELETE CASCADE,
  folder_id INTEGER NOT NULL REFERENCES library_folders(folder_id) ON DELETE CASCADE,
  mtime     INTEGER NOT NULL,                   -- last_write_time, ns
  size      INTEGER NOT NULL                    -- bytes
);
CREATE INDEX IF NOT EXISTS idx_monitored_files_folder ON monitored_files(folder_id);

CREATE TABLE IF NOT EXISTS playlists (
  playlist_id  INTEGER PRIMARY KEY AUTOINCREMENT,
  name         TEXT NOT NULL,
  last_played  INTEGER NOT NULL,
  tab_order    INTEGER NOT NULL DEFAULT 0,
  kind         TEXT NOT NULL DEFAULT 'manual' CHECK (kind IN ('manual', 'auto')),
  expression   TEXT,                            -- set exactly when kind = 'auto'
  CHECK ((kind = 'auto') = (expression IS NOT NULL))
);
```

* `playlist_items` holds rows for manual playlists only.
* Autoplaylist row order:
  ```sql
  CREATE TABLE IF NOT EXISTS autoplaylist_order (
    playlist_id INTEGER NOT NULL REFERENCES playlists(playlist_id) ON DELETE CASCADE,
    song_id     INTEGER NOT NULL REFERENCES songs(song_id) ON DELETE CASCADE,
    position    INTEGER NOT NULL,
    PRIMARY KEY (playlist_id, song_id)
  );
  CREATE INDEX IF NOT EXISTS idx_autoplaylist_order_songid ON autoplaylist_order(song_id);
  ```
* `songs.filepath` is `NOT NULL UNIQUE` and always holds the canonical path
  (invariant 1).
* The `songs` cascade never removes a monitored song in practice: only
  `releaseSongs` deletes song rows, and it skips monitored ones.
* The release check (invariant 3) as SQL, for tests:
  ```sql
  SELECT s.song_id FROM songs s
  WHERE s.song_id IN (:pks)
    AND NOT EXISTS (SELECT 1 FROM playlist_items pi WHERE pi.song_id = s.song_id)
    AND NOT EXISTS (SELECT 1 FROM monitored_files mf WHERE mf.song_id = s.song_id);
  ```
  At runtime the same check runs on the in-memory sets.

## Classes

### Backend (follows "Backend classes do not leak UI")

* `MonitoredLibrary` (owned by `MainWindow`, constructed before
  `PlaylistCollection`): owns `library_folders`, `monitored_files` and, in
  memory, each song's folder and stats, folder availability and the failed
  set. Runs and queues scans ("Scan"; requests made in one call start one
  scan), debounces watcher events and polls offline roots. Reports
  `songsEntered`, `songsUpdated`, `songsLeft` (left the monitored library;
  the row may survive) and `folderAvailabilityChanged`. It does not know
  playlists: `PlaylistCollection` releases songs on `songsLeft`. The parse
  function and the `IFileSystemMonitor` (null: no live updates; it must
  outlive the library) are constructor arguments, so tests inject them.
* `LibraryScanner`: the worker-side steps 2-5 of "Scan", plain data in
  (folders and scope, snapshot, failed set, registry copy, parse function)
  and out (availability; new, changed and gone entries; new failures).
* `IFileSystemMonitor`: recursive watches, with `pathsChanged` and
  `folderNeedsRescan` always delivered on the main thread.
  `EfswFileSystemMonitor` queues efsw's events (`handleMissedFileActions`
  becomes `folderNeedsRescan`); `DummyFileSystemMonitor` lets tests emit
  them.
* `SongLibrary`: `search(expr, candidatePks)` (in `filepath` order) and
  `matches(expr, pk)` fill the autoplaylists; `prepareParsedSong` fills a
  parsed song's computed fields; `addTolibrary` canonicalises `filepath`
  (invariant 1).
* `Playlist`: an auto playlist (`playlists.kind`) loads its saved order and
  refuses the write operations; `PlaylistCollection` sets its rows with
  `resetRows`, `appendRows` and `removeRowsByPk`, which never emit
  `songPksRemoved`, so they never cause a release.
* `PlaylistCollection`: creates, edits and fills the autoplaylists and keeps
  them current ("Membership"); `releaseSongs` (invariants 3 and 4), called
  for a manual playlist's `songPksRemoved`, removing a manual playlist, and
  `songsLeft`.
* `PlaybackController`: nothing of its own; `PlaybackManager` and the
  policies handle pks that are no longer in the playlist.

### Frontend

* `LibraryFoldersPage` (Settings > Library): the watched folders, marked
  "Offline" or "Live updates unavailable"; Add…, Remove, Rescan now, and
  "Remove missing songs" for a folder offline because empty; scan progress.
  It calls `MonitoredLibrary` directly, so its actions apply at once, not on
  the dialog's OK.
* `AutoPlaylistDialog`: name and expression, with parse errors and a live
  match count over the monitored library. Opens from `File > New
  Autoplaylist…`, Library Search's "Save as autoplaylist" (with its
  expression) and the tab context menu's "Edit Autoplaylist…". OK needs a
  name and a valid expression.
* `PlaylistTabs`: autoplaylist tabs have their own icon, or a warning icon
  with the error as tooltip; context menus follow "Operations by playlist
  kind".
* Status bar: a permanent label shows "Scanning library… 120 / 3000" during
  a scan, then "1 library folder offline" if any folder is offline, and is
  hidden otherwise.

## Startup and shutdown

Startup:

1. `loadLibrary` (worker).
2. In `finishSetUpPlaylist`: `MonitoredLibrary::load()`, which reads its
   two tables on the main thread (they are small) and adds a watch for every
   watched folder, then `PlaylistCollection::loadAll()`
   (manual playlists, then autoplaylists filled from the stored monitored
   set; custom columns are already loaded, so expressions using them parse).
3. `PlaybackController::setReady(true)`. Playback is available from here,
   before any scan.
4. Start the startup scan. The watches are already in place, so changes made
   during the scan are caught too. Folders found offline lose their watch and
   go to the 60 s poll.
5. Cloud sync starts from the event loop, in the Play Stats plugin (it does
   not wait for these steps; [cloud-sync.md](cloud-sync.md)).

Shutdown: a running scan's results are dropped (its `QFutureWatcher` is a
child of `MonitoredLibrary`, as `loadLibrary`'s is of `MainWindow`). Nothing
is lost: the next startup scan redoes the work.

## Tests

* `tst_monitoredlibrary`: every row of "Events and transitions"; scan
  classification; walk rules (symlinks, hidden files, extension case);
  overlapping folders rejected; queued and merged scans; stale results
  skipped at apply; offline cases 1 and 2 release nothing; "Remove missing
  songs"; debouncing and the offline poll, with `DummyFileSystemMonitor`.
* `tst_playlistcollection`, `tst_autoplaylist`: `parseSongFilter`, the
  `Playlist` kind and row methods, membership and re-evaluation (including
  `songsRefreshed` and a plugin's values reaching every song of a key), row
  order, invalid expressions.
* `tst_settingsdialog_ui`: the Library page. `tst_librarysearchdialog_ui`:
  Library Search's scope, boolean check and "Save as autoplaylist".
  `tst_playlisttabs` and `tst_mainwindow`: `AutoPlaylistDialog`, tab
  behaviour, action enabling.
* `tst_playlistsearch`: `SongMatcher` / `matchPlaylistRows` in the three
  modes (the inputs under "Mode"); `tst_playlistsearchdialog_ui`:
  `PlaylistSearchDialog`.
* `tst_efswfilesystemmonitor`: the real watcher on a temporary directory.

## Not covered

* Tracking moved files by inode or file id to keep their pk. A move is a
  remove + add; play stats survive through the key.
* A list of unreadable files in the UI.
* Watching network shares reliably. Native events may not arrive for remote
  changes; the startup scan and "Rescan now" still pick them up.
* Recomputing stored computed values after custom fields change (the `TODO`
  in `MainWindow`'s `customFieldsChanged` handler). Autoplaylists
  re-parse their expressions then, but evaluate the stored values.
* Filling in a new `attr:` column for songs parsed before it existed. The
  scan only re-parses files whose stats changed (Add Folder doesn't re-parse
  either); "Refresh Playlist Metadata" fills them in.

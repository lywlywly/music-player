# Plan: Monitored Library, Autoplaylists and Playlist Search

Status: implemented (phases 1-6). The watcher uses efsw 1.7.3 from
`third_party/efsw`. Where the code differs from the first plan, this file
was updated to match it.

## Summary

* **Monitored library**: the user picks folders. The app keeps the audio
  files under them in the library automatically: new files are added,
  changed files are re-read, deleted files leave.
* **Autoplaylists**: playlists defined by a library expression. Their rows
  are the monitored songs that match it, kept up to date as songs change.
* **Playlist Search**: a new search panel over the playlist shown in the UI.
  The existing Library Search now covers the monitored library only.

The existing behaviour (manual playlists filled with Open / Add Folder) stays
as it is. It is called the **manual library** below.

## Key decisions

### Library model

* **Two independent libraries, one song table.** The manual and the
  monitored library never change each other's membership. They share one
  `songs` row per file path, so a file that is in a watched folder and also
  added by hand is one song with one set of tags and play stats.
* **A song row lives while either library holds it**, and is deleted only
  when neither does. `PlaylistCollection::releaseSongs` is the only place
  that deletes songs.
* **Watched folders cannot be nested or overlap.** Adding one that is inside,
  or contains, a watched folder is rejected with a message.
* **No DB migration.** The schema is created on a fresh database (delete the
  old one during development).

### Change detection

* **The scan is the source of truth; the watcher is only a hint.** A scan
  runs at startup, on "Rescan now", when a folder is added, and for paths the
  watcher reports. The watcher (efsw) never changes the library by itself.
* **Changes are detected by mtime + size**, not by checksums.
* **The scan does not follow symlinks and skips hidden entries** (names
  starting with `.`, which includes macOS `._*` files).
* **An unavailable folder is offline, not empty.** If a watched folder's
  drive is unmounted (its root is missing, or is an empty directory while the
  library has files for it), nothing is removed; the folder is shown as
  offline.
* **The app's own tag writes don't cause a re-parse.** After a Song
  Properties save, the stored file stats are updated, so the watcher event
  the save causes finds nothing to do.
* **Unreadable files are remembered only until restart**, not stored in the
  DB, and are retried once they change.

### Expressions

* **Filters must be conditions.** Library Search, autoplaylists and the
  expression mode of Playlist Search accept an expression only if it parses
  and its static type is boolean (`parseSongFilter`);
  `parseLibraryExpression` itself also accepts text and numbers. A bare word like `davis` parses as a text literal, so
  this check is what tells an expression apart from plain text.

### Autoplaylists

* **Autoplaylists are views of the monitored library only.** A song that is
  only in manual playlists never appears in one.
* **Rows are read-only but sortable.** No adding, removing or clearing;
  the expression decides the contents. Column sort works and, as for manual
  playlists, is not saved.
* **If the playing song stops matching, it keeps playing.**
* **No "whole monitored library" view.** Use an autoplaylist with the
  expression `true` (verified: it parses as a boolean expression).

### Search

* **Library Search covers the monitored library only** and stays
  expression-only.
* **Playlist Search is a separate results panel** over the playlist shown in
  the UI (not the playing one). It accepts plain text or an expression.
  Double-click plays a result; "Show in playlist" only selects it.

### UI

* Watched folders are managed on a new **Settings > Library** page.
* Autoplaylists are created from `File > New Autoplaylist…` or from Library
  Search ("Save as autoplaylist"), and edited from the tab context menu.

## Core model

### Terms

* **Song row**: one row in `songs`, one per canonical file path, with a
  `song_id` (pk). It holds tags, `attr:` and computed values, and links to an
  identity that holds play stats. In memory the pk is the index into
  `SongLibrary::songs`.
* **Owner**: a library that keeps song rows alive. There are exactly two.
  * **Manual library**: songs in at least one manual playlist
    (`playlist_items`).
  * **Monitored library**: songs recorded in `monitored_files`. A file is
    recorded there when a scan finds it under a watched folder and TagLib can
    read it, and stays recorded until a scan of an **online** folder no
    longer finds it (or can no longer read it), or its folder is removed.
* **Watched folder**: a directory in `library_folders`, scanned recursively.
  It is **online** or **offline** (see "Offline folders").
* **Autoplaylist**: a playlist with `kind = 'auto'` and an expression. Its
  rows are computed, never stored. It is not an owner.

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
   `PlaylistCollection::releaseSongs(pks)` (formerly
   `removeSongsInNoPlaylist`, extended to ask `MonitoredLibrary::holds`). It is the only caller of
   `SongLibrary::removeSongs` and the only emitter of
   `songsRemovedFromLibrary`.
5. **Views first, then delete.** A song leaves every view (playlist rows,
   autoplaylist rows) before `releaseSongs` deletes it, so no view ever
   holds a deleted pk. Manual removals already work this way (rows removed,
   then `songPksRemoved`); for monitored songs, `PlaylistCollection` handles
   `songsLeft` by first removing autoplaylist rows, then calling
   `releaseSongs`.
6. **Autoplaylists own nothing.** They never write `playlist_items`, never
   cause a release, and removing one deletes only its `playlists` row.
7. **Each monitored file belongs to exactly one watched folder**
   (`monitored_files.folder_id`), because folders don't overlap and the walk
   doesn't follow symlinks.
8. **Song data is shared.** Tag edits, metadata refresh, play stats and cloud
   sync write the shared row, and every view of that pk refreshes
   (`PlaylistCollection::notifySongChanged`), whichever library the change
   came through.

### Membership per library

| | Manual library | Monitored library |
|---|---|---|
| Stored in | `playlist_items` | `monitored_files` |
| Grain | (playlist, song); a song can be in many playlists | one row per song |
| Joins by | Open, Add Folder, Create playlist from Library Search | a scan finds a readable file under an online watched folder |
| Leaves by | Remove from Playlist, Clear Playlist, Remove Tab; released when the song is in no manual playlist | a scan of an online folder no longer finds the file (deleted, moved out, renamed to an unsupported extension) or can't read it any more, or the folder is removed |
| File missing on disk | row kept with its last tags, skipped on play (today's behaviour; there is no "missing" marker) | leaves on the next scan, unless its folder is offline (then kept, skipped on play) |
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
| File moved (within or between watched folders) | W at A | W at B | gone at A + new at B: B gets a new pk, play stats carry over by identity; a manual playlist holding A keeps A as a missing file |
| Folder removed from the watch list | W or M+W | - or M | delete `library_folders` row (cascades `monitored_files`), remove the watch; `songsLeft`, then `releaseSongs` for all its pks |
| Folder offline at scan | W or M+W | same | nothing released; `folderAvailabilityChanged(id, false)` |
| Offline folder available again | W or M+W | per scan | `folderAvailabilityChanged(id, true)`, add the watch, then a normal scan of it |
| "Remove missing songs" on a folder offline because empty | W or M+W | - or M | scan that skips the availability check: every file counts as gone |
| App writes tags (Song Properties save) | W or M+W | same | refresh row, store new file stats; the watcher event that follows finds no change |
| Last manual reference removed | M+W | W | delete `playlist_items` row, `releaseSongs`: row kept, still in autoplaylists |
| same | M | - | `releaseSongs`: row deleted (today's behaviour) |
| New file unreadable by TagLib | - | - | not added; kept in the failed set (see "Unreadable files") |

A deleted and re-added file gets a new pk (`song_id` is `AUTOINCREMENT`), and
its play count comes back through its identity, as today.

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
   symlinks and skipping hidden files and directories, and `stat` each one.
   Metadata only. The extension list moves from `playlistdialogs.cpp` to a
   backend header (`audiofileformats.h`) shared with Open and Add Folder.
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
   * Fill computed fields and the identity key with the live registry
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
  and Library Search still finds them. Playing one hits the existing
  missing-file skip.
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

* `SongPropertiesDialog` already calls `SongLibrary::refreshSongFromFile`
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
  in memory. The expression must be a valid boolean expression (see
  "Expressions").
* Re-evaluated for:
  * the pks in `songsEntered` and `songsLeft`;
  * the pk in every `PlaylistCollection::notifySongChanged(pk)`: file
    changes, Song Properties, play stats, cloud sync. So `play_count > 10`
    gains a song as soon as its count passes 10;
  * the pks of a "Refresh Playlist Metadata" run: `Playlist` gets a new
    signal `songsRefreshed(pks)`, emitted after `songsReset`, which
    `PlaylistCollection` handles like `notifySongChanged` for each pk (this
    also refreshes the same songs in other playlists, which used to stay
    stale);
  * all monitored pks when the expression is edited, and when custom fields
    change: `MainWindow`'s `customFieldsChanged` handler calls
    `PlaylistCollection::reloadAutoPlaylistExpressions()`, which re-parses
    every expression first.
* Play stats are per identity: `markSongPlayedAtStart` and
  `incrementPlayCount` change every song sharing the identity, so
  `PlaybackController` notifies every pk of the identity
  (`SongLibrary::songPksSharingIdentity(pk)`), not only the playing one, and
  autoplaylists on `play_count` stay right for duplicates.
* Songs in offline folders stay members (they are still in the monitored
  library).

### Row order

* On load and after an expression edit, rows are in `filepath` order.
* Songs that start matching are appended at the end.
* Column-header sort reorders rows in memory only (`Playlist::sortByField`,
  as for manual playlists). The next launch starts in `filepath` order
  again.

### Invalid expression

A parse error, a non-boolean expression, or a field that no longer exists:
the autoplaylist has no rows, and its tab shows a warning icon with the error
as a tooltip. The expression text is kept so the user can fix it. Loading
never fails because of an invalid expression; the playlist is just empty.

### Playback

* `last_played` (the resume pointer, a pk) is saved as for manual playlists.
* If the playing song stops matching (or leaves the monitored library while
  its row survives), it keeps playing. Next/Prev treat it as "current song
  not in the playlist", as they already do for removed songs, and queued
  entries for rows that left are skipped (`PlaybackManager::next` already
  checks `containsPk`).
* If the song row is deleted, `songsRemovedFromLibrary` stops playback, as it
  does today.

### Operations by playlist kind

| Operation | Manual | Auto |
|---|---|---|
| File > Open / Add Folder (into the current playlist) | yes | disabled |
| Remove from Playlist / Clear Playlist (context menu and File menu) | yes | hidden in the context menu, disabled in the File menu |
| Column sort | yes (not saved) | yes (not saved) |
| Rename / reorder / remove tab | yes | yes; removing releases no songs |
| Properties / Open Containing Folder / Refresh Playlist Metadata | yes | yes (writes the shared row) |
| Play Next / Add to Playback Queue / play / follows-cursor | yes | yes |
| Edit Autoplaylist… (name and expression) | - | yes |

The existing rule that the last playlist can't be removed applies to both
kinds.

`Playlist` refuses the write operations for an auto playlist (`addFiles`
returns no pks, `addSong` / `addSongByPk` return false, `removeSongs` and
`clear` do nothing, each with a `qWarning`). The UI never offers them.

## Search

| | Library Search (existing, changed) | Playlist Search (new) |
|---|---|---|
| Scope | monitored library | rows of the playlist shown in the UI, manual or auto |
| Input | boolean library expression | plain text or boolean library expression |
| Results | songs, in `filepath` order | rows, in the playlist's current order |
| Actions | Create playlist (manual), Save as autoplaylist | Play, Show in playlist, Add to Playback Queue, Properties |
| Opens from | `Library > Search` (existing) | `Edit > Search Playlist…` (`QKeySequence::Find`: `Ctrl+F`, `Cmd+F` on macOS; the only Edit menu item) |

### Library Search

* `SongLibrary::search(expr, candidatePks)` takes the candidate set and
  returns the matches in `filepath` order. `LibrarySearchDialog` passes
  `MonitoredLibrary::songPks()`.
* It uses the shared boolean check, so `davis` now shows "Expression must be
  a condition" instead of a meaningless result.
* With no watched folders, the dialog shows "No watched folders. Add one in
  Settings > Library." instead of an empty result.
* "Create playlist" still creates a manual playlist from the results, so
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
  * Switching tabs re-targets the panel and re-runs the query. `PlaylistTabs`
    gets a `currentPlaylistChanged(Playlist *)` signal for this.
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
  rows (debounced), so an autoplaylist changing underneath stays correct.
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

Created fresh in `DatabaseManager` (`PRAGMA foreign_keys = ON` is already
set, which the cascades need). New tables, and `playlists` with its two new
columns (`kind`, `expression`):

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

* `playlist_items` is unchanged and holds rows for manual playlists only.
* `songs.filepath` is already `NOT NULL UNIQUE`; it now always holds the
  canonical path (invariant 1).
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

* `MonitoredLibrary` (new, owned by `MainWindow`, constructed before
  `PlaylistCollection`)
  * Constructor: `SongLibrary &`, `const ColumnRegistry &`,
    `DatabaseManager &`, a parse function (defaults to
    `SongParser::parse`; tests inject one) and an `IFileSystemMonitor *`
    (null: no live updates; it must outlive the library).
  * Owns `library_folders`, `monitored_files`, and in memory
    `pk -> {folder_id, mtime, size}`, folder availability and the failed set.
  * API: `load()` (reads both tables on the main thread; they are small),
    `addFolder(path)` (canonicalises, rejects overlap by path component,
    case-insensitively on macOS and Windows), `removeFolder(id)`,
    `folders()`, `rescan()`, `removeMissingSongs(folderId)`,
    `scanPaths(paths)` (path scopes: stale results and watcher events),
    `isScanning()`, `holds(pk)`, `songPks()`, `noteSongWritten(pk)`,
    `checkOfflineFolders()` (the 60 s poll). A call that requests several
    scopes (`rescan()` over several folders, `scanPaths`) starts one scan.
  * Signals: `songsEntered(pks)`, `songsUpdated(pks)`, `songsLeft(pks)`
    (left the monitored library; the row may survive),
    `folderAvailabilityChanged(folderId, online)`, `scanProgress(done,
    total)`, `scanFinished`.
  * Runs and queues scans (see "Scan"), debounces watcher events, polls
    offline roots.
  * Does not know about playlists; `releaseSongs` is called by
    `PlaylistCollection` in reaction to `songsLeft`.
* `LibraryScanner` (new): the worker-side steps 2-5 of "Scan", as a free
  function or a stateless class. Input is plain data (folders and scope,
  snapshot, failed set, registry copy) plus a parse function (defaults to
  `SongParser::parse`; tests inject one, like `SongParseFn`). Output is plain
  data: per-folder availability; new, changed and gone entries with parsed
  songs and stats; new failed entries.
* `SongLibrary`
  * `search(expr, candidatePks)`, in `filepath` order; also fills
    autoplaylists.
  * `matches(expr, pk)`, for autoplaylist membership.
  * `prepareParsedSong(MSong &) const`: computed fields and identity key,
    split out of `loadPreparedSongFromFile`.
  * `addTolibrary` canonicalises `filepath` (invariant 1).
  * `songPksSharingIdentity(pk)`, from `songIdsByIdentityId_`.
* `parseSongFilter(const SongLibrary &, const QString &)` (new, next to
  `parseLibraryExpression`): parse plus the boolean check, with the error
  "Expression must be a condition". Used by Library Search, autoplaylists,
  `AutoPlaylistDialog` and `SongMatcher`.
* `IFileSystemMonitor` (rewritten)
  * QObject API: `addPath` (returns whether the recursive watch was added),
    `removePath` (returns whether the path was watched); signals `pathsChanged(QStringList)` and
    `folderNeedsRescan(QString)`, always delivered on the main thread.
  * `EfswFileSystemMonitor`: efsw-backed (one `efsw::FileWatcher`, recursive
    watches), forwards events with queued signals.
    `handleMissedFileActions` maps to `folderNeedsRescan`.
  * `DummyFileSystemMonitor` for tests: records watches and lets the test
    emit events.
* `Playlist` gets a kind (`Manual` / `Auto`) from `playlists.kind`.
  * Auto: `loadState` reads only `last_played`; the write operations are
    refused (see "Operations by playlist kind").
  * New, for auto playlists only: `resetRows(pks)` (reset signals),
    `appendRows(pks)` (insert signals) and `removeRowsByPk(pks)` (the same
    remove signals as `removeSongs`). None of them emits `songPksRemoved`,
    so they never cause a release.
  * New signal `songsRefreshed(pks)` after `refreshMetadataFromFiles`.
* `PlaylistCollection`
  * Constructor adds `MonitoredLibrary &` (existing tests construct one with
    no monitor), exposed as `monitoredLibrary()` for `openSongProperties`.
  * Adds `createAutoPlaylist(name, expression)`,
    `setAutoPlaylistExpression(id, expression)`, `isAuto(id)`,
    `autoPlaylistExpression(id)`, `autoPlaylistError(id)` and
    `reloadAutoPlaylistExpressions()`, and the signal
    `autoPlaylistExpressionChanged(id)` after either of the setters.
  * `notifySongsChanged(pks)` (public), used for the identity fan-out.
  * `releaseSongs(pks)` replaces `removeSongsInNoPlaylist`: keeps pks in no
    manual playlist and not `MonitoredLibrary::holds`, calls
    `SongLibrary::removeSongs`, emits `songsRemovedFromLibrary` (signal
    unchanged, so `PlaybackController` keeps its connection). Called for
    `songPksRemoved` of manual playlists, removing a manual playlist, and
    `songsLeft`.
  * `loadAll()` loads both kinds; auto playlists are filled from
    `MonitoredLibrary::songPks()`.
  * Keeps auto playlists current (see "Autoplaylists > Membership").
* `PlaybackController`: notify every pk sharing the identity after
  `markSongPlayedAtStart` and `incrementPlayCount`. No other new logic;
  `PlaybackManager` and the policies already handle pks that are no longer in
  the playlist.

### Frontend

* `SettingsDialog`, new "Library" page (`LibraryFoldersPage`): watched
  folders with "Offline" and "Live updates unavailable" markers; Add…,
  Remove, Rescan now; "Remove missing songs" for a
  folder offline because empty (offline although its root exists); scan
  progress. The page calls `MonitoredLibrary` directly, and its actions apply
  at once, not on the dialog's OK.
* `AutoPlaylistDialog` (new): name and expression editor with parse errors
  and a live match count over the monitored library. Opens from
  `File > New Autoplaylist…`, Library Search's "Save as autoplaylist" (the
  last searched expression), and the tab context menu's "Edit
  Autoplaylist…". OK needs a name and a valid expression.
* `LibrarySearchDialog`: monitored scope, boolean check, no-watched-folders
  hint, "Save as autoplaylist".
* `PlaylistSearchDialog` (new): see "Playlist Search".
* `PlaylistTabs`: `currentPlaylistChanged` signal; autoplaylist tabs get a
  distinct icon (and the warning icon, with the error as tooltip, for an
  invalid expression), updated on
  `PlaylistCollection::autoPlaylistExpressionChanged`; context menus follow
  "Operations by playlist kind".
* `MainWindow`: Open, Add Folder and Clear Playlist actions are disabled
  while an autoplaylist tab is shown.
* Status bar: a permanent label shows "Scanning library… 120 / 3000" during
  a scan, then "1 library folder offline" if any folder is offline, and is
  hidden otherwise. The playback message beside it is unchanged.

## Startup and shutdown

Startup:

1. `loadLibrary` (worker), unchanged.
2. In `finishSetUpPlaylist`: `MonitoredLibrary::load()`, which also adds a
   watch for every watched folder, then `PlaylistCollection::loadAll()`
   (manual playlists, then autoplaylists filled from the stored monitored
   set; custom columns are already loaded, so expressions using them parse).
3. `PlaybackController::setReady(true)`. Playback is available from here,
   before any scan.
4. Start the startup scan. The watches are already in place, so changes made
   during the scan are caught too. Folders found offline lose their watch and
   go to the 60 s poll.
5. Cloud sync.

Shutdown: a running scan's results are dropped (its `QFutureWatcher` is a
child of `MonitoredLibrary`, as `loadLibrary`'s is of `MainWindow`). Nothing
is lost: the next startup scan redoes the work.

## Phases

Each phase is separately testable and committable.

1. **Monitored library, without the watcher.** Schema, path canonicalisation
   in `addTolibrary`, `audiofileformats.h`, `releaseSongs`,
   `MonitoredLibrary` (no monitor), `LibraryScanner`; startup scan and
   `rescan()`; `noteSongWritten`. Tests: every row of "Events and
   transitions"; scan classification; walk rules (symlinks, hidden files,
   extension case); overlapping folders rejected; queued and merged scans;
   stale results skipped at apply; offline cases 1 and 2 release nothing;
   "Remove missing songs". Existing `PlaylistCollection` tests are updated
   for the new constructor.
2. **Autoplaylist backend.** `parseSongFilter`, `Playlist` kind and row
   methods, membership and re-evaluation (including `songsRefreshed` and
   identity fan-out), row order, invalid expressions. Tests in
   `tst_playlistcollection` and a new `tst_autoplaylist`.
3. **UI.** Settings Library page, `AutoPlaylistDialog`, Library Search scope,
   boolean check and "Save as autoplaylist", tab behaviour, action enabling.
   UI tests in the style of `tst_settingsdialog_ui`.
4. **Playlist Search.** `SongMatcher` / `matchPlaylistRows` with tests for the
   three modes (using the inputs listed under "Mode"), then
   `PlaylistSearchDialog` with a UI test. Needs `parseSongFilter` from
   phase 2, nothing else.
5. **Live watcher.** `IFileSystemMonitor` rewrite, `EfswFileSystemMonitor`,
   `DummyFileSystemMonitor`, debouncing and the offline poll in
   `MonitoredLibrary`. The rewrite breaks the unused prototype, so it is
   deleted here: `QFileSystemMonitor`, `FileSystemComparer`,
   `ChecksumCalculator`, `tst_fsmonitor`, `tst_checksum` (already disabled),
   and `find_package(OpenSSL)` if nothing else uses it. (efsw itself is
   already set up as the `third_party/efsw` submodule, built statically on
   all platforms.) `tst_efswfilesystemmonitor` checks the real watcher on a
   temporary directory.
6. **Docs.** Update `docs/architecture.md`, `docs/expressions.md` (boolean
   check), `README.md` (features) and `roadmap.md`.

## Not covered

* Tracking moved files by inode or file id to keep their pk. A move is a
  remove + add; play stats survive through identity.
* A list of unreadable files in the UI.
* Saving autoplaylist (or manual playlist) sort order.
* Watching network shares reliably. Native events may not arrive for remote
  changes; the startup scan and "Rescan now" still pick them up.
* Recomputing stored computed values after custom fields change (the existing
  `TODO` in `MainWindow`'s `customFieldsChanged` handler). Autoplaylists
  re-parse their expressions then, but evaluate the stored values.
* Filling in a new `attr:` column for songs parsed before it existed. The
  scan only re-parses files whose stats changed (Add Folder doesn't re-parse
  either); "Refresh Playlist Metadata" fills them in.

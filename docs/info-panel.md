# Plan: Info Panels (Properties and cover art)

Status: implemented. The Spectrum panel is described in
[spectrum.md](spectrum.md), the Lyrics panel's classes in
[plugins/lyrics.md](plugins/lyrics.md), and where the panels go in
[layout-editing.md](layout-editing.md).

## Summary

* The info panels are **Properties**, **Lyrics** (`LyricsPanel`, from the
  lyrics plugin), **Spectrum** and the playing song's **cover art**. Each is
  an element of the editable layout ([layout-editing.md](layout-editing.md)).
  The default layout has Properties at the top left of the main window with
  the cover below it; Lyrics and Spectrum are inserted from edit mode, for
  example as tabs beside Properties.
* **Properties** shows the song selected in the shown playlist: its file
  (name, folder, path, size, modified and created times), its audio
  properties, its main tags, the plugins' fields (play stats) and its
  watched folder. It
  follows the selection; it does not show the playing song.
* The **cover art** shows the playing song's embedded cover
  (`CoverArtPresenter`).

## Key decisions

* **The selected song, not the playing song.** The panel shows the first
  selected row of the shown playlist (`PlaylistTableView::firstSelectedRow`,
  the smallest selected row). With no row selected, or no playlist shown, it
  shows "No song selected"; it does not fall back to the playing song. The
  playing song already has lyrics, cover art and the status bar.
  * With `Playback -> Cursor follows playback` on (the default),
    `PlaylistTabs::navigateIndex` selects the song that starts on Next, Prev,
    Random and skips (`setCurrentIndex` clears the other rows), so the panel
    then follows playback too. That comes from the selection, not from the
    panel.
* **One song.** With several rows selected, the first (smallest) selected
  row is shown. A multi-song summary (count, total size) is out of scope.
* **Stored values, no re-parse.** Audio properties, tags and plugin fields come
  from the `MSong` already in `SongLibrary`, the values the playlist table
  shows. The panel never calls `SongLibrary::refreshSongFromFile`: that
  parses the file and writes the DB, which must not happen on every
  selection change.
  * Like the table, the panel can show tags older than the file when the file
    was changed outside the app and is not in a watched folder. Opening Edit
    Properties... re-reads the file (`openSongProperties`), but only a save
    notifies the views (`songUpdated` -> `notifySongChanged`), so a cancelled
    dialog leaves the panel and the table as they were.
* **File stats are read at display time** with `QFileInfo` on the song's
  `filepath`, on the main thread. They are not stored: `monitored_files`
  holds mtime and size only for monitored songs, and only as of the last
  scan.
* **No stat on offline folders.** If the song is monitored and its watched
  folder is offline, the file is not touched (a stat on an unmounted network
  drive can block) and the File section shows "Folder offline".
* **Frontend only, apart from one query.** Building the rows uses column
  titles (`ColumnDefinition::title`), `FieldValue::display()` and locale
  formatting, which backend classes must not use (see "Backend classes do
  not leak UI" in [architecture.md](architecture.md)). So the row builder is
  a frontend free function, tested without widgets. The only backend change
  is `MonitoredLibrary::folderOf`.

## UI

### Layout

The default panel layout ([layout-editing.md](layout-editing.md)):

```
horizontal splitter [320, 960]
├── vertical splitter [300, 500]
│   ├── songInfoPanel (SongInfoPanel, Properties)
│   └── coverLabel    (cover art)
└── playlistTabs
```

* `spectrumWidget` (`SpectrumWidget`) and each plugin panel (`IPanelProvider`;
  the lyrics panel's object name is `lyricsPanel`) start outside the layout.
* `songInfoPanel` and `spectrumWidget` have a minimum size of 120 x 240 and
  `coverLabel` of 120 x 120, set in `mainwindow.ui`, wherever the layout
  places them.
* In a layout with tabs, the current tab is saved with the layout
  (`layout/tree`). A saved layout whose plugin is disabled keeps that
  plugin's place, with "*Name* is not available" in place of the panel.

### Lyrics while hidden

`LyricsPresenter` is not told whether its panel is shown: the Lyrics panel
keeps receiving positions while hidden (outside the layout, or on a tab not
in front), and `LyricsPanel` keeps scrolling to the current line. That alone
would show the wrong line when the panel comes back: a hidden panel is not
resized (a `QTabWidget` page lives in a `QStackedLayout`, which sizes only
the current page), so a window or splitter resize meanwhile leaves it at its
old size, and `scrollToIndexCenter` centers by the viewport height at the
time it runs.

So `LyricsPanel` keeps the index of its current line (`currentLine_`, set by
`scrollToIndexCenter`, reset by `setLyricsPanel`) and re-centers on it without
animation in `showEvent` and `resizeEvent`, stopping a scroll animation
started while hidden. This also keeps the line centered when the shown panel
is resized. Not while the user holds following by scrolling
([plugins/lyrics.md](plugins/lyrics.md), "Scroll hold"): the view stays where
they scrolled it.

### Properties panel contents

Read-only rows of label and value in a `QScrollArea`, grouped by section.
Values are word-wrapped `QLabel`s with `Qt::TextSelectableByMouse`, so they
can be copied. A row whose value is empty, or whose field the song does not
have, is left out, and so is a section with no rows.

| Section | Row | Value |
|---|---|---|
| File | File name | `QFileInfo::fileName()` |
| | Folder | `QFileInfo::absolutePath()` |
| | Path | the full `filepath` |
| | Status | "File not found" or "Folder offline"; left out otherwise |
| | Size | `QLocale::formattedDataSize(size)`, exact byte count in the tooltip |
| | Modified | `QFileInfo::lastModified()` |
| | Created | `QFileInfo::birthTime()`, "Unknown" when invalid |
| Audio | Codec, Duration, Bitrate, Sample rate, Channels | `codec`, `duration`, `bitrate` (with " kbps"), `sample_rate` (with " Hz"), `channels` |
| Tags | Title, Artist, Album, Disc no, Track no, Date, Genre | the built-in columns of the same ids |
| Library | each plugin field, in registration order (Play Count, Last Played with the Play Stats plugin) | `ColumnRegistry::pluginFieldIds()` (`play_count`, `last_played_timestamp`) |
| | Watched folder | the folder's path, with "(offline)" when offline; left out for a song not in the monitored library |

* The Audio, Tags and Library labels (except Watched folder) are the
  `ColumnDefinition` titles from `ColumnRegistry`, so they read the same as
  the table headers. Values are `FieldValue::display()`, so
  Duration, Channels and Last Played use their display kinds
  (`DurationSeconds`, `ChannelLayout`, `EpochSecondsDateTime`) as in the
  table. The table shows Bitrate and Sample rate as bare numbers; the panel
  adds the units (TagLib reports kb/s and Hz).
* Modified and Created use the table's date format (`yyyy-MM-dd HH:mm:ss`,
  local time), so they match Last Played.
* Paths are shown with `QDir::toNativeSeparators` (`filepath` is the
  canonical path, see `SongLibrary::addTolibrary`). They wrap at their
  separators, so the full path is visible; a path segment longer than the
  column scrolls horizontally rather than being clipped.
* A missing file (`!QFileInfo::exists`) shows Status "File not found" and no
  Size, Modified or Created; File name, Folder and Path are still shown from
  the stored path, and the other sections are unchanged. An offline folder
  shows Status "Folder offline" the same way, without looking at the file.
* `birthTime()` is invalid on file systems or platforms that do not record a
  creation time, hence "Unknown".
* Two buttons below the rows: **Open Containing Folder** (`revealSongFile`,
  which already warns for a missing file) and **Edit Properties...**
  (`openSongProperties`, the existing Song Properties dialog). Both are
  disabled with no song shown. They sit side by side when both fit and are
  stacked otherwise (`SongInfoPanel::resizeEvent`); their `Ignored`
  horizontal size policy keeps them from setting the panel's minimum width,
  since the column can be 120 px wide.
* The panel name "Properties" is shared with the Song Properties dialog; the
  button says "Edit Properties..." to tell them apart.

## Classes

### `MonitoredLibrary::folderOf` (backend)

* `std::optional<Folder> folderOf(int songPk) const`: the watched folder
  holding the song (`entries_` gives its `folderId`, `folders_` the
  `Folder`), or none for a song it does not hold. `Entry` is private, so
  `holds` was not enough to tell the folder or whether it is online.

### `SongInfo`, `buildSongInfo` (frontend, `songinfo.{h,cpp}`)

* `SongInfo` is display data: the shown pk (`-1` for none, `isEmpty()`) and
  ordered sections of rows (label, value, tooltip).
* `SongInfo buildSongInfo(int songPk, const SongLibrary &,
  const MonitoredLibrary &, const ColumnRegistry &)` reads
  `SongLibrary::getSongByPK` and, unless the song's folder is offline, one
  `QFileInfo` of its `filepath`.
* It handles a song with no `filepath`: `SongLibrary::removeSongs` replaces a
  removed song with an empty `MSong` (`getSongByPK` still returns it), and
  `song.at("filepath")` would throw. It then returns an empty `SongInfo`,
  which the panel shows as "No song selected". This covers a rebuild that runs
  after its song was removed from the library.

### `SongInfoPanel` (frontend, `songinfopanel.{h,cpp}`)

* The Properties panel's widget, promoted in `mainwindow.ui` (in
  `layoutElements`). Built in code (no `.ui`), since its rows depend on the
  song.
* `setInfo(const SongInfo &)` (an empty one shows "No song selected") and
  `info()`.
* Emits `revealRequested(songPk)` and `editPropertiesRequested(songPk)` from
  its buttons, for the song it shows; it does not call the dialogs itself.
  The buttons are enabled only while a song is shown.

### `SongInfoPresenter` (frontend, `songinfopresenter.{h,cpp}`)

* A child of `MainWindow`. Holds the shown playlist (a
  `QPointer<Playlist>`), the song pk to show, and the panel (a
  `QPointer<SongInfoPanel>`: the UI widgets are destroyed before the window's
  later children).
* `showSong(Playlist *, int songPk)`; `songPk < 0`, or no playlist, shows no
  song.
* Rebuilds the panel when:
  * the song to show changes;
  * the shown playlist sends `songDataChanged(row)` for the song's row
    (`getIndexByPk`). Every song write outside playlist operations reaches it
    through `PlaylistCollection::notifySongsChanged`: Song Properties saves,
    a plugin's provided values (play stats), monitored rescans
    (`songsEntered`, `songsUpdated`), and Refresh (`songsRefreshed`);
  * a watched folder goes offline or online
    (`MonitoredLibrary::folderAvailabilityChanged`).
* Rebuilds are debounced (`kRebuildDelayMs`, 100 ms, a single-shot timer), so
  holding an arrow key, or removing several selected rows (one remove pair
  per row), does one stat at the end.
* It rebuilds whether or not the Properties panel is shown: a rebuild is one
  `QFileInfo` and a few labels. Rejected: deferring rebuilds while the panel
  is hidden, which does not help the slow case (a stat on a slow network
  path blocks just the same once the panel is shown), and offline folders
  are skipped anyway.
* It does not watch `PlaylistCollection::songsRemovedFromLibrary`. A song
  leaves the library only when it is in no manual playlist and not monitored,
  so the shown song cannot: it is in the shown playlist, and an autoplaylist
  removes its row (which changes the selection) first. A debounced rebuild
  that runs after a removal builds an empty `SongInfo` (see above).

### `CoverArtPresenter` (frontend)

* Shows the playing song's embedded cover in the cover label
  (`coverLabel`), scaled to the label on each of its resizes (an event
  filter on the label), so it follows the label wherever the layout puts it.
  The scaled pixmap fits the label's contents rect and the label keeps its
  minimum size, so rescaling does not grow the label.
* `MainWindow` calls `showSong` on `PlaybackController::songStarted`, after
  `SystemMediaPresenter::showSong` (which clears the system artwork), and
  `clear()` on stop.
* The cover comes from `SongParser::extractCoverImage`; a file it cannot open
  (for example moved or deleted) logs a warning and shows no cover.
* Emits `artworkChanged(imageData)` for a song with an embedded cover;
  `MainWindow` connects it to `SystemMediaPresenter::setArtwork`
  ([playback.md](playback.md)).

### `PlaylistTabs`

* `selectedSongChanged(Playlist *playlist, int songPk)`: the song of
  `firstSelectedRow()` of the current tab, `-1` with no selected row,
  `(nullptr, -1)` with no tab. Emitted only when the pair changes.
* Re-evaluated on these signals of the current view's model and selection
  model (`watchCurrentSelection`, connected in `onTabChanged` and
  disconnected from the previous view), and on the tab change itself:
  * `selectionModel()->selectionChanged`. Removing selected rows sends it
    too: the selection model drops them in `rowsAboutToBeRemoved`, while the
    `Playlist` still has its old rows, so the row-to-pk lookup is right.
    Removing other rows only renumbers the selection, which keeps its songs,
    so it needs no re-check;
  * `modelReset`: a reset (an autoplaylist's `resetRows`, Refresh's
    `songsReset`) clears the selection without `selectionChanged`;
  * `layoutChanged`: a sort or drag moves the selected songs with their rows
    (`PlaylistTableModel` moves persistent indexes by pk), again without
    `selectionChanged`. With several rows selected, a different song can end
    up first, so the pk can change.
* These are the model's signals, not the `Playlist`'s: the selection model
  connected to the model in `setModel`, before `PlaylistTabs` connects, so
  the selection is already updated when the handler runs.

### `MainWindow`

* Creates `SongInfoPresenter`, and in `finishSetUpPlaylist` connects
  `PlaylistTabs::selectedSongChanged` to `showSong`. Nothing is selected at
  startup, so the panel starts with "No song selected".
* Connects the panel's buttons to `revealSongFile(this, song)` and
  `openSongProperties(this, pk, songLibrary, columnRegistry_, *playlists_)`.
  `setUpLayout` places the panels ([layout-editing.md](layout-editing.md)).

## Edge cases

* **Selected row removed** (Remove from Playlist, or the song leaves an
  autoplaylist): `selectionChanged`, then `selectedSongChanged` with the next
  first selected song or `-1`.
* **Song removed from the library** (`releaseSongs`): only songs in no
  manual playlist, so never the shown song of a manual playlist; an
  autoplaylist removes the row first, so the selection has already moved.
* **Refresh** (tab context menu): the playlist is reset, which clears the
  selection, so the panel shows "No song selected" afterwards. This follows
  the existing reset behaviour.
* **Shown playlist removed**: the tab change sends `selectedSongChanged` for
  the new tab; the presenter's `QPointer` covers the time in between.
* **File changed on disk**: for a monitored song, the scan re-reads it and
  `songsUpdated` reaches the row, so Size and Modified update. For a song
  only in manual playlists, they update on the next rebuild (selection
  change, or a song-data change such as a play count).
* **File moved or deleted**: "File not found"; Open Containing Folder shows
  the existing warning from `revealSongFile`.
* **Startup**: tabs exist only after `finishSetUpPlaylist`; until then the
  panel shows "No song selected".

## Tests

* `tst_songinfo`: `buildSongInfo` with a temp file of known size and mtime
  (File, Audio with units, Tags, Library and Watched folder rows), a song
  only in a manual playlist (no Watched folder row), a missing file, a song
  in an offline watched folder (Status "Folder offline", no Size), and a
  removed song (empty). Then `SongInfoPresenter`: debounced showing, a
  rebuild for its own row only, and a rebuild when the song's folder goes
  offline. Then `SongInfoPanel`'s buttons: enabled only with a song, sending
  its pk, side by side or stacked by width.
* `tst_monitoredlibrary`: `folderOf` for two folders, an offline folder, an
  unmonitored pk, and a song whose folder was removed.
* `tst_playlisttabs`: `selectedSongChanged` on selection (smallest row
  first), on tab change, on a sort that puts a different selected song first
  and not on one that keeps it first, on removing selected rows and not on
  removing an unselected one, and on an autoplaylist reset.
* `tst_lyricspanel`: the current line is centered after a line change and a
  resize while hidden, then show; new lyrics forget the current line. Its
  click, hover and scroll hold tests are in
  [plugins/lyrics.md](plugins/lyrics.md).
* `tst_mainwindow`: Properties in the default layout; the Lyrics (from the
  plugin in `Tests/plugins`), Properties and Spectrum tabs of a saved
  layout, with the saved tab restored; Properties shows the selected song in
  the real window, and Edit Properties... opens Song Properties.

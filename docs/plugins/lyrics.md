# Plan: Lyrics plugin

Status: implemented.

The Lyrics info panel tab: the playing song's lyrics, with the line for the
play position highlighted and centered, a click on a line to play from it, and
a Settings page for the font and highlight color. The plugin system it is
built on is in [plugin-system.md](plugin-system.md); the info panel's tabs in
[info-panel.md](../info-panel.md).

## Code: `src/plugins/lyrics/`

Built and deployed as every plugin is ([plugin-system.md](plugin-system.md),
"Building a plugin").

* `mylyrics`: `LyricsPanel` (+ `.ui`), `LyricsManager`, `LyricsLoader`,
  `LyricsPresenter`, `LyricsSettingsPage`, and `loadLyricsSettings` /
  `saveLyricsSettings` (the `lyrics/*` keys, for the presenter and the
  page).
* `lyricsplugin`: the root object `LyricsPlugin`, implementing `IPlugin`,
  `IPanelProvider` and `ISettingsPageProvider`, and its metadata,
  `lyricsplugin.json` (`{ "id": "lyrics", "name": "Lyrics" }`).
* `createPanel` creates a `LyricsPanel` (object name `lyricsPanel`) and its
  `LyricsPresenter`, a child of the panel, with the host's `IPlayerHost` and
  `IPlayerControl`. The presenter implements `IPlayerListener`, adds itself
  in its constructor and removes itself in its destructor, so it listens for
  the panel's lifetime, while the host exists. The plugin keeps the
  presenters (`QPointer`s, as the panels are deleted by the tab widget).
* `LyricsPresenter` owns the `LyricsManager`, shows the playing song's lyrics
  in its `LyricsPanel`, plays from a clicked line, and applies the style
  settings to the panel. `LyricsManager` holds the lyrics, moves the current
  line with the play position, and gives a line's timestamp
  (`lineStartMs`).
* `LyricsPanel` keeps the index of its current line and re-centers on it,
  without animation, in `showEvent` and `resizeEvent`: the tab keeps getting
  positions while hidden, but a `QTabWidget` sizes only its current page, so
  the hidden panel can be resized later ([info-panel.md](../info-panel.md)).
  It handles the hover, the click and the scroll hold ("Seeking from a line,
  hover and the scroll hold").

## Where lyrics come from (`LyricsLoader`)

* On play, `PlaybackController` refreshes the song's metadata from its file
  once, so the `TrackInfo` has the file's current tags.
* `LyricsLoader::resolveLyrics` picks the first non-empty of `fields`
  (`attr:unsynced_lyrics`, `attr:lyrics`), then of `tagFields`
  (`unsynced_lyrics`, `lyrics`). If none is set, or it has no timestamped
  lines, the `.lrc` file is the fallback.
* The `.lrc` fallback (`LyricsLoader::getLyrics`) opens
  `<home>/Music/<artist> - <title>.lrc` only, where `<home>` is `USERPROFILE`
  on Windows and `HOME` elsewhere; it does not look next to the audio file.
* Known issue (Windows): the `.lrc` file name is built as a UTF-8
  `std::string` and converted to `std::filesystem::path`, which on Windows
  decodes narrow strings with the active ANSI code page, not UTF-8. A
  non-ASCII artist or title (for example CJK) maps to a different file name,
  so its `.lrc` is not found. macOS and Linux are unaffected, as is Windows
  with the system-wide UTF-8 code page. Fix: build the path from UTF-8
  explicitly (`std::u8string` or a wide path), as `SongParser` does for
  TagLib with `MultiByteToWideChar`.

## Settings (`LyricsSettingsPage`, `lyricssettings.h`)

* Four keys, read and written only by `loadLyricsSettings` /
  `saveLyricsSettings`: `lyrics/use_system_default_font`,
  `lyrics/font_family`, `lyrics/font_size` and `lyrics/highlight_color`.
  Defaults: the system font unless a family is stored, the application
  font's size, and highlight `#0064ff` (also used for a stored color that is
  not valid).
* `font_family` is always stored, even while the system font is used. With
  the system font on, the font combo is disabled but still shows the stored
  family.
* On page load, the combo selects the stored family, or the font Qt
  substitutes for it when it is not installed. The combo is editable, and OK
  stores its text as typed: an unknown or empty family leaves the
  application font's families in effect.
* `highlight_color` is picked with a color dialog and colors the current
  line.
* `apply()` (on the Settings dialog's OK) writes the keys and sends
  `applied`, on which the plugin re-applies them to the open panels.
* The lyrics font is the application font at the stored size. With the
  system font off, the stored family is put first in its family list, so the
  application font's CJK fallback families ([ui-styling.md](../ui-styling.md))
  still cover characters the chosen font lacks.

## Seeking from a line, hover and the scroll hold

The lyrics panel is a list of lines that can be clicked:

* **Hover**: the line under the pointer is highlighted as the current line
  is, and the pointer is a pointing hand over a line.
* **Click**: a single click on a line plays from its timestamp: it seeks
  there, and a paused song resumes.
* **Scroll hold**: scrolling the panel with the wheel or touchpad stops it
  following playback, so the view is not pulled back while the user looks
  for a line. Following resumes after a seek from a line, with new lyrics,
  or 3 s after the user last scrolled or moved the pointer over the panel.

The click calls `IPlayerControl::seek`, the command A-B Repeat uses, then
`IPlayerControl::play` ([plugin-system.md](plugin-system.md), "Commands:
`IPlayerControl`").

### Classes

* `LyricsPanel` installs an event filter on its text edit's viewport, which
  handles press, double click, release, mouse move, leave and wheel, and
  sends `lineClicked(int index)`. It holds the hovered line, the pressed
  line, the pointer's last position over the viewport, and the scroll hold
  (a flag and a single-shot `QTimer`). `updateSelections` sets the extra
  selections from the current and hovered lines, for a line change, the
  hover and `setHighlightTextColor`.
* `LyricsPresenter` connects `lineClicked` to
  `seek(lyricsManager_.lineStartMs(index))`, then `play()`.
* The panel sends a line number, not a time: it holds only the text, and
  the timestamps stay in `LyricsManager`. The presenter joins them, as it
  does for the highlight.
* An event filter in `LyricsPanel`, not a `QTextEdit` subclass promoted in
  `lyricspanel.ui`: the hover, the click and the hold share the panel's
  current line and scroll animation, which a subclass would have to reach
  back into.

### Click

* **On release, on the line that was pressed**, with the left button. The
  press notes the line under the pointer; the release seeks only if it is
  over the same line. Moving off the line before releasing cancels, as with
  a button. Lines are compared by index, so a line change that scrolls
  another line under the pointer between press and release cancels too; the
  scroll lasts 300 ms every few seconds, so this is rare. The second press
  of a double click arrives as a double-click event, which counts as a
  press, so two quick clicks on different lines both seek.
* **Only on a line.** The point's y, plus the scroll value, must lie in the
  line's block rectangle (`blockBoundingRect`, in document coordinates):
  `cursorForPosition` alone returns the nearest block, so a click below the
  last line would seek to the last line. An empty panel still has one empty
  block, so the panel also keeps the number of lyrics lines, and a block
  past it is not a line. The x is not checked: the rectangle spans the
  document's width, as the hover highlight does, so a click beside a short
  centered line counts.
* **Blank timestamped lines are lines**: in LRC they mark instrumental
  breaks, so they are places in the song worth seeking to.
* **No text interaction, no focus and no context menu** on the text edit
  (`Qt::NoTextInteraction`, `Qt::NoFocus`, `Qt::NoContextMenu`): a single
  click cannot both select text and seek, and the panel is a list of lines.
  Without focus, the keyboard does not scroll it either, so the wheel is the
  only way the user scrolls it. With no text interaction the text edit's
  context menu would be empty.
  * `setTextInteractionFlags(Qt::NoTextInteraction)` instead of
    `setReadOnly(true)`: a text edit is read-only when its flags lack
    `TextEditable`, and `setReadOnly(true)` sets `TextSelectableByMouse`.
  * The filter consumes press, double click, release and mouse move
    (returns `true`): with no text interaction the text edit has nothing to
    do with them, and consuming them leaves the viewport's cursor to the
    panel alone. It does not consume wheel or leave.
  * Rejected: keeping selection and seeking only on a click without a drag.
    It needs a drag threshold to tell the two apart, and one gesture would
    then do two different things.
  * Cost: lyrics cannot be selected or copied from the panel.
* **A click while paused plays from the line.** Clicking a line asks to
  hear it; seeking alone would leave the song paused at a line the user
  cannot hear, and the highlight still on the old line, as GStreamer sends
  no positions while paused (`GstAudioPlayer::updatePosition` returns
  before emitting unless playing).
  * `play` is the Play button's command, so it is called on every click:
    while playing it changes nothing, and the plugin need not know whether
    the song is paused (the host reports no pause). A line can be clicked
    only while a song is loaded, as a stop clears the lyrics, so `play`
    starts the shown playlist only in the race below, as the last song
    ends.
  * The seek comes first, and commands run in order: the song resumes at
    the line, not for a moment where it was paused.
  * Rejected: seeking only, leaving the paused state. Seeking to a line
    and pressing Play would take two actions for what one click means.
* **The highlight is not moved on click.** It follows the next position,
  as every command's effect comes through the listener events
  ([event-ordering.md](../event-ordering.md), "Plugin commands: the
  contract"), so the highlight moves only with positions. That is the
  queued seek's event-loop turn plus up to one position update (100 ms with
  GStreamer, `GstAudioPlayer`'s position timer).
* **A click just before a song change seeks the next song**: the known race
  of plugin seeks ([event-ordering.md](../event-ordering.md), "A plugin seek
  lands in the next song"), closed by the playback generation. Likewise, a
  click as the last song of the playlist ends runs its `play` after the
  stop, which starts the shown playlist as the Play button would; the
  generation check would drop both commands.

### Hover

* Mouse tracking on the viewport. The hovered line gets the current line's
  highlight: a full-width extra selection in the highlight color
  (`lyrics/highlight_color`). One highlight style for the panel, set in one
  place; the pointing hand tells a hovered line from the current one.
* Rejected: the selection colors (the palette's `Highlight` background and
  `HighlightedText` text). A second style beside the highlight color, and a
  hovered current line would need an order between the two.
* The hovered line follows the content, not only the pointer: on the scroll
  bar's `valueChanged` and on new lyrics, while the pointer is over the
  viewport, it is found again from the pointer's last position. Auto-follow
  and the wheel move lines under a pointer that does not move, which sends
  no mouse move. The position is the one the last mouse move gave, not
  `QCursor::pos()`: the events already carry it in viewport coordinates, and
  a test's mouse moves do not move the system cursor.
* Leaving the viewport clears it and forgets the position.
* The cursor is `Qt::PointingHandCursor` over a line, the arrow elsewhere.

### Scroll hold

* **A wheel event holds following.** It stops the scroll animation, sets the
  hold and restarts its timer; the filter lets the text edit scroll as
  usual. Touchpad scrolling arrives as wheel events.
* **While held**, a line change moves the current line's color but does not
  scroll, and `showEvent` and `resizeEvent` do not re-center (the re-center
  of [info-panel.md](../info-panel.md), "Lyrics while hidden").
* A Ctrl+wheel zooms the text, as a `QTextEdit` without `TextEditable` does;
  it is a wheel event, so it holds following too.
* **The hold ends**:
  * when the timer fires, 3 s (`kScrollHoldMs`) after the last wheel event
    or mouse move over the viewport. While held, mouse moves restart it
    too, so the view does not move while the user aims at a line. The view
    scrolls back to the current line with the usual animation;
  * on a click that seeks, without scrolling: the seek's line change
    scrolls to the clicked line. Scrolling back to the old line first would
    move the view away and back;
  * on new lyrics (`setLyricsPanel`, which a stop calls too), whose view
    starts at the first line.
* 3 s is a constant, not a setting: nothing asks to change it.
* Rejected: holding until the pointer leaves the panel. A pointer left
  resting over the panel would stop it following for the rest of the song.

## Tests

* `tst_lyrics`, `tst_lyricspanel`: link `mylyrics`; `resolveLyrics` cases
  take `TrackInfo`.
* `tst_lyricspanel`, besides the re-center ([info-panel.md](../info-panel.md)):
  * A click on line N sends `lineClicked(N)`; a release on another line, a
    click below the last line, and a click on an empty panel send nothing.
  * A mouse move over line N gives it a selection in the highlight color,
    beside the current line's, and the pointing hand; a `QEvent::Leave`
    sent to the viewport clears both.
  * After a wheel event sent to the viewport, a line change does not
    scroll; once the hold ends the current line is centered. A click ends
    the hold at once: the next line change scrolls.
  * The presenter, with an `IPlayerHost` and an `IPlayerControl` made by
    the test (the control records commands): after a `trackStarted` with
    timestamped lyrics, a click on a line seeks to its timestamp, then
    plays.
* `tst_plugins`, through the built plugin in `Tests/plugins`:
  * The panel shows the tag lyrics of the song that starts and clears on
    stop.
  * The settings: an open panel's font puts the stored family before the
    application font's families; the page shows the stored size, system
    font choice and color; the system font disables the family combo;
    `apply()` writes the four `lyrics/*` keys (the family as typed) and
    restyles the open panel. Which font the combo selects for a stored
    family is Qt's substitution, checked by hand: the offscreen platform has
    no fonts.
* By hand: a click plays from the line, playing and paused, with GStreamer
  and Qt Multimedia, with no moment of the paused position heard; the hover
  highlight; touchpad scrolling on macOS and Windows, the hold, and the
  return after 3 s.

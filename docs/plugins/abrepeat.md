# A-B Repeat plugin

Loops a section of the playing song: the user marks A and B in a submenu of
Playback, and playback jumps back to A each time it reaches B. The plugin
system it is built on is in [plugin-system.md](plugin-system.md).

| Item | Does |
| --- | --- |
| Set A | Marks the current position as A and clears B |
| Set B | Marks the current position as B; enabled once A is set. Playback then loops |
| Clear | Removes A and B; enabled once A is set |

* The items show the marked times ("Set A (1:23)"), so the loop's state is
  visible without a widget of its own.
* While A and B are set, a position at or past B seeks to A. A seek past B
  (with the slider) jumps back to A at the next position; a seek before A
  plays on until B.
* Set B at or before A does nothing: B is marked while listening past A, so
  this is rare. Swapping A and B instead would make a loop the user did not
  mark.
* A song start (the same song again too) and a stop clear A and B; they are
  not saved. The items are disabled while no song is loaded: before the
  first song starts and after a stop.
* Three items, not one cycling through Set A, Set B and Clear: what that
  item did next would depend on a state the menu has to spell out, and
  marking A again in a set loop would take a Clear first.

## Code: `src/plugins/abrepeat/`

Built and deployed as every plugin is ([plugin-system.md](plugin-system.md),
"Building a plugin").

* `myabrepeat`: `ABRepeat`, the loop and its `IPlayerListener`, no UI. It
  holds A, B, the last position and whether a song is loaded. A position at
  or past B calls `seek(A)`; `trackStarted` and `stopped` clear A and B.
* `abrepeatplugin`: the root object `ABRepeatPlugin` (`IPlugin`,
  `IMenuProvider`), which builds the menu, and its metadata,
  `abrepeatplugin.json` (`{ "id": "abrepeat", "name": "A-B Repeat" }`).
* `initialize` creates the `ABRepeat` with the host's `IPlayerControl` and
  adds it as a listener; `shutdown` removes and destroys it.
* `createMenu` builds the submenu (object name `abRepeatMenu`). Its
  `aboutToShow` sets the items' text and enabled state from the `ABRepeat`,
  so the menu needs no notification from it.
* Set A and Set B use the last position the listener received, as the host
  has no position query; the error this adds is in "Precision".

## Precision

About 0.1 s with GStreamer, with a gap at each jump:

* **Marks**: A and B are the last position received, up to one position
  update (100 ms) before the click.
* **Reaching B**: the loop seeks on the first position at or past B, so it
  plays up to 100 ms past B. The position is the audio output's, and a
  flushing seek drops the buffered audio, so the overshoot is what is heard.
* **Landing at A**: `GstAudioPlayer::setPosition` seeks accurately
  ([playback.md](../playback.md), "Seeking"), to the exact position, except
  in a VBR MP3 without a seek index, whose position is estimated.
* **The jump**: the flushing seek restarts the output, a short gap or click.
* With Qt Multimedia, Qt's media backend sets how often the position
  arrives, and seeks on macOS are inaccurate (roadmap.md Known Issues).
* **Near the end of a song**: with B in the last position update (100 ms
  with GStreamer), the end-of-stream can come before a position at or past
  B, and the next song plays. Or the position reporting B and the
  end-of-stream are pending in the same event-loop turn, and the queued
  seek lands in the next song ([event-ordering.md](../event-ordering.md),
  "Known races").

Options to tighten it, not planned:

* **A timer for B in the plugin**: a position within one update of B starts
  a single-shot `Qt::PreciseTimer` for the rest, which seeks when it fires.
  The overshoot drops to the timer's and the position's error, a few
  milliseconds, with no API change.
* **A gapless loop in the backend**: a segment seek from A to B, seeking
  again on GStreamer's segment-done message, loops sample-exact without a
  gap. It is a loop command for the backend, not a plugin `seek`, and Qt
  Multimedia has no equivalent.

## Costs

* The loop's precision is about 0.1 s, with a gap at each jump
  ("Precision").
* Set A, Set B and Clear have no shortcuts: plugin menu items have none
  ([plugin-system.md](plugin-system.md), "Capabilities: what a plugin
  contributes").

## Tests

* `tst_abrepeat`, linking `myabrepeat`, with an `IPlayerControl` made by the
  test that records seeks: a position at or past B seeks to A, one before B
  does not; Set B at or before A is ignored; Set A clears B; a song start
  (the same song again too) and a stop clear the loop.
* `tst_mainwindow::abRepeatMenu_loopsThePlayingSong`: the A-B Repeat submenu
  is last in Playback, its items disabled with no song; with a song playing,
  Set A and Set B show their times, and a position past B seeks the player
  to A. Items are triggered after the menu's `aboutToShow`, as when the user
  opens it: a disabled action does not trigger.
* By hand: an A-B loop with GStreamer and with Qt Multimedia; the submenu's
  text and enabled state in the macOS native menu bar; B near a song's end.

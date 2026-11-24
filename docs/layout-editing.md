# Plan: Custom layout editing

Status: implemented. The playback buttons, seek bar and volume are not
elements yet (see "Out of scope").

## Summary

* The main window's central area, below the playback controls, is a
  **layout**: a tree of splitters and tabs whose leaves are **elements**
  (Playlist, Properties, Spectrum, Cover Art, and each plugin panel). The
  playback buttons, seek bar and volume above it are fixed.
* **View > Edit Layout** turns on edit mode. In it, right-clicking anywhere in
  the layout opens a menu for the clicked node instead of the widget's own
  menu: Insert or Replace with, Delete, Cut, Paste, the node's own items,
  and a Parent submenu. Left clicks still work normally. **View > Reset
  Layout...** goes back to the default layout.
* The layout is saved as JSON in `layout/tree` after every change, including
  splitter drags and tab switches.
* The default layout is Properties above the cover on the left and the
  playlist on the right, with no tabs. Lyrics, Spectrum and any other plugin
  panel start outside the layout, to be inserted from edit mode.
* The model is DeaDBeeF's design mode (its GTK UI's `widgets.c`): a JSON tree
  of typed nodes, empty slots as their own node type, a node whose element is
  missing kept unchanged, and the same context menu. The differences, and
  why, are in "Key decisions".

## Key decisions

### Elements are single, existing widgets that the layout places

Each element is one widget that `MainWindow` already has (`playlistTabs`,
`songInfoPanel`, `spectrumWidget`, `coverLabel`, each plugin's panel from
`createPanel`). The layout only places it: building puts it in a splitter or
tab, and an element that is not in the tree waits, hidden, in the hidden
`layoutElements` widget. Each element appears at most once.

* Why: the presenters stay one-to-one with their widgets and keep their
  state across edits.
  * `SpectrumPresenter` offers the one `PcmTap` that
    `PlaybackController::setPcmTap` takes.
  * `CoverArtPresenter` also sends the artwork to the system media controls.
  * `SongInfoPresenter` holds a debounce timer.
  * `PlaylistTabs` holds the tabs, views and models, and playback depends on
    `currentPlaylist()`.
  * A plugin creates its panel once (`LyricsPlugin::createPanel` creates the
    panel's presenter with it).

  So `MainWindow` wires the presenters to the widgets in `ui`, wherever the
  layout puts them, and the plugin API is unchanged (`IPanelProvider/1`).
* The element widgets are declared in `mainwindow.ui` as children of
  `layoutElements`, with their minimum sizes, so they are promoted there.
  Plugin panels are created with `layoutElements` as their parent.
  `layoutElements` and the layout host are descendants of `centralWidget`,
  which `setupUi` creates before `PluginManager`, so the plugin panels are
  destroyed before the plugins shut down.
* An element outside the tree is hidden, so it behaves as a hidden tab does:
  the spectrum withdraws its tap, and the lyrics panel re-centers when shown
  again. The playlist can be left out too. The app still works: the shown
  playlist is the hidden `PlaylistTabs`' current tab. Both reference players
  allow it as well.
* Rejected: DeaDBeeF's model, where each node creates a new widget instance
  from a factory, so an element can appear several times. Two spectrums
  would need a tap fan-out, two covers a split between loading the artwork
  and showing it, and two playlist views a rule for which one is "shown".
  Re-creating `PlaylistTabs` on each edit would also lose its tabs' state.
* Cost: an element can't appear twice, for example two spectrums.

### The tree: splitters of two, tabs, elements, placeholders

| Node | JSON | Children |
| --- | --- | --- |
| Horizontal or vertical splitter | `{"type": "hsplitter" \| "vsplitter", "sizes": [a, b], "children": [...]}` | exactly 2 |
| Tabs | `{"type": "tabs", "current": i, "titles": [...], "children": [...]}` | 1 or more |
| Element | `{"type": "element", "id": "playlist"}` | none |
| Placeholder (empty slot) | `{"type": "placeholder"}` | none |

* Built-in element ids are `playlist`, `properties`, `spectrum` and `cover`
  (`MainWindowLayout::kPlaylist`, ...). A plugin panel's id is its plugin's
  id (`lyrics`). The layout module does not know any of them: the app names
  the elements it registers.
* **Delete and Cut leave a placeholder** (as in DeaDBeeF), so a container
  never changes its number of children through them. A splitter always has
  two, and no edit has to collapse or merge containers. A layout with three
  side by side nests two splitters.
* **An element whose id is not available is kept.** That is a plugin
  disabled in Settings > Plugins, skipped, or removed. Its node stays in the
  tree, is saved unchanged, and shows "*Name* is not available", with the
  plugin's name from its `PluginEntry`, or the id when the plugin is gone.
  Once the plugin is loaded again, its panel is back where it was.
* **Splitter sizes** are `QSplitter::sizes()`, written on `splitterMoved` and
  applied with `setSizes`, which scales them to the splitter's size. A new
  splitter starts at `[1, 1]`, so it is split in half. Rejected:
  `QSplitter::saveState`, whose bytes are opaque and can't be part of a JSON
  node.
* **A tab's label** is the name the user gave it (Rename Tab), else its
  element's title (`panelTitle()` for a plugin panel); for a container, the
  titles of the elements inside it, joined with " / ", or "Empty". So an
  unnamed tab follows its content.
* **Tab names belong to the tabs node**, one per tab (`titles`, empty for an
  unnamed tab), not to the node in the tab. A name therefore stays with its
  tab when the tab's content is replaced, split, deleted or cut, and moves
  with the tab on Move Tab Left or Right. `titles` is left out of the JSON
  when no tab is named; when present it must have one string per tab.
  Rejected: a name on the node in the tab, which every edit that replaces
  the node would have to carry over.
* Rejected: splitters with any number of children. Inserting and deleting
  would then change their count, and sizes would have to be redistributed.
* Rejected: DeaDBeeF's splitter lock modes (keep one side's size on resize).
  They can be added later as a splitter setting.

### Storage: one key, the default built in code

* `layout/tree` holds the tree as compact JSON (`PanelLayout::toJson`). It is
  written after every edit, splitter move and tab switch.
* A missing key gives the default layout. A key that does not parse, or
  breaks the table above (unknown type, wrong child count, missing or
  negative sizes, `current` out of range, `titles` not one string per tab),
  also gives the default, with a `qWarning`. The key is user data from
  outside the program, so it is checked once, at load
  (`PanelLayout::fromJson`). Inside the program the tree is valid by
  construction, and the edit operations do not re-check it.
* An element id that appears twice (in a hand-edited key) is kept the first
  time, in tree order; later copies become placeholders. Paste applies the
  same rule (see "Edit mode").
* The default layout (`MainWindowLayout::defaultLayout`, the app's):
  * `hsplitter [320, 960]`, so the info side starts at a quarter of the
    width (the reason is in [architecture.md](architecture.md), "App
    composition");
  * on the left, `vsplitter [300, 500]` (3 to 5) of `properties` and
    `cover`;
  * on the right, `playlist`.

  It has no tabs and holds none of the plugins' panels, so it is the same
  whatever plugins are loaded. `spectrum` and the plugin panels (Lyrics)
  start in the store. Cost: Lyrics and the spectrum are not shown until the
  user inserts them in edit mode.
* An incompatible format change later gets a new key, as DeaDBeeF's
  `gtkui.layout.<version>` does, rather than a version field and migration
  code now.
* Rejected: converting the keys of the earlier fixed layout
  (`layout/main_splitter`, `layout/info_splitter`, `infopanel/current_tab`)
  into the first tree. Cost: an existing user's splitter sizes and current
  info tab are reset once.
* A plugin's panel, like any element not in the tree, is shown only once
  the user inserts it. Both reference players behave the same way. Adding
  it automatically would mean guessing where it belongs.

### Edit mode

* View > Edit Layout is a checkable action. It is not saved, so the app
  always starts with it off.
* While it is on, `PanelLayoutEditor` is an event filter on the application.
  It takes every `QEvent::ContextMenu` whose receiver is in the layout, so a
  widget's own menu (playlist rows, tabs, header, lyrics) does not open. The
  node is found by walking from the receiver up its parents to the first
  widget the view built or placed (`PanelLayoutView::pathOf`), so a click on
  a splitter handle or a tab bar reaches its container. Other events, and
  requests outside the layout (dialogs, the menu itself), pass through.
* A translucent overlay over the node's widget, a child of the window that
  ignores the mouse, shows which node the menu is for while the menu is
  open. Rejected: DeaDBeeF hides the node's children to paint the highlight,
  which in Qt would change the layout under the menu.
* The menu for a node:
  1. its title, disabled;
  2. **Insert** (on a placeholder) or **Replace with** (otherwise): the
     elements not in the tree, then Splitter (Left and Right), Splitter (Top
     and Bottom) and Tabs. A new splitter's first child is the replaced node
     and its second is a placeholder. A new tabs node holds the replaced
     node. On a placeholder, a new splitter or tabs node holds placeholders.
     Replacing a container with an element drops its subtree: its elements
     leave the tree;
  3. **Delete** and **Cut** (not on a placeholder): replace the node with a
     placeholder. Cut keeps the subtree to paste;
  4. **Paste** (enabled when something was cut): replaces the node with the
     cut subtree. Any element in it that has been inserted elsewhere since
     becomes a placeholder (the same rule as at load). The cut subtree is
     then cleared;
  5. for a tabs node: **Add Tab** (appends a placeholder);
  6. for a tab of a tabs node: **Move Tab Left**, **Move Tab Right**,
     **Rename Tab...** and **Remove Tab**, each disabled where it can't
     apply (first tab, last tab, only tab). Rename Tab asks for a name,
     prefilled with the tab's label. An empty name, or one equal to the
     label the tab would have unnamed, makes it unnamed again;
  7. **Parent**: the parent's menu, as a submenu, when the node has a parent.
     It reaches a container whose children cover it, as in DeaDBeeF.
* The menu is opened with `popup()`, not `exec()`, so the event filter
  returns at once. The chosen action runs from the menu's own event, never
  while an event is being delivered to a layout widget that the action's
  rebuild deletes. The menu is deleted from the event loop after it hides
  (`aboutToHide` comes before the action triggers). Rename Tab's dialog,
  which runs an event loop of its own, is opened from the event loop too
  (a queued call), after the menu's action has returned.
* The cut subtree is kept in memory only, not on the clipboard.
* Reset Layout asks for confirmation, then replaces the tree with the
  default and saves it.
* Rejected: **Copy**. Every element appears at most once, so a copy could
  only paste placeholders. Cut and Paste move a subtree.
* Rejected: **Undo** (DeaDBeeF records a layout snapshot per edit). Reset
  Layout is the way back, and every element can be inserted again from the
  menu. Cost: a single mistaken edit can't be reverted on its own.
* Rejected: `QDockWidget` with `QMainWindow::saveState`. It gives floating
  and dockable panels, but not free nesting of splitters and tabs in the
  central area. Its state is opaque bytes.

### Rebuilding on each edit

Each edit changes a copy of the tree, and `PanelLayoutView::setLayout`
rebuilds the whole widget tree from it:

1. Disconnect the old containers' `splitterMoved` and `currentChanged`.
2. Move every placed element back to `layoutElements`, hidden explicitly.
3. Delete the old root widget, unless it is an element (then it was moved
   in step 2).
4. Build new containers, place the elements, and apply each node's sizes and
   current tab.
5. Only then connect the new containers' signals.

* The order of steps 1 and 5 matters. Taking pages out of a `QTabWidget`,
  or adding its first page, emits `currentChanged`. Connected at those
  times, it would write a stale or zero index into the tree.
* Moving the elements out first means deleting the containers never deletes
  an element. A `QSplitter` or `QTabWidget` deletes its children. They are
  hidden explicitly so a widget that follows its visibility (the spectrum)
  sees the hide, and each element placed in a splitter is shown again; a
  tab widget shows and hides its pages itself.
* The layout has a handful of widgets, so a full rebuild costs nothing
  noticeable. It also needs no per-container insert, remove and replace
  code (DeaDBeeF's `append`/`remove`/`replace` callbacks).
* Splitter sizes and the current tab are written into the tree as they
  change (`splitterMoved`, `currentChanged`), so the tree is always current
  and a rebuild loses nothing.
* The view is a `QObject` child of the layout host, created before the
  containers it builds there, so it is destroyed before them: their signals
  during the window's destruction reach nothing.

### Cover art follows its label

`CoverArtPresenter` rescales the cover on each of its label's
`QEvent::Resize` (an event filter on the label), so the cover follows the
label in any splitter or tab, and when a hidden label is shown (Qt delivers
the pending resize then). The scaled pixmap fits the label's contents rect,
and the label keeps its 120 x 120 minimum, so rescaling does not grow the
label.

## Classes

The layout module, `src/layout/` (static library `mylayout`), holds
`PanelLayout`, `PanelLayoutView` and `PanelLayoutEditor`. Like the spectrum
module, it depends on Qt only, never on app classes. It knows no element
ids, no default layout and no app menu; the app passes them in.

* **The module's tests enforce it.** `tst_panellayout`,
  `tst_panellayoutview` and `tst_panellayouteditor` link `mylayout`, Qt and
  QtTest only, not `MYLib` (their own block in `src/Tests/CMakeLists.txt`),
  so a module file that uses an app class fails to link them. `mylayout`
  linking Qt alone is not enough: a static library is not linked by itself,
  and the app's executables link `MYLib` beside it, which would resolve the
  app symbols. They use no settings, files or audio, so they need none of
  the other tests' helpers (the settings and GStreamer bootstraps).
* Cost: the empty-slot text is a constructor argument of the view.
* Rejected: keeping the files in `src/` with the app, where nothing would
  keep app knowledge out of them.

### `PanelLayout` (frontend, no widgets; `layout/panellayout.{h,cpp}`)

* The tree: `PanelLayout::Node` (kind; element id; sizes; current tab; tab
  titles; children). A node is addressed by its `Path`, the child indexes
  from the root.
* `fromJson(QByteArray)` returns `std::optional<PanelLayout>`, checking the
  table above and turning repeated elements into placeholders; `toJson()`.
* `node(path)`, `placedElements()` (in tree order, for the Insert menu).
* Edits: `replaceWithElement(path, id)`, `split(path, kind)`,
  `wrapInTabs(path)`, `take(path)` (Delete and Cut; leaves a placeholder),
  `paste(path, subtree)`, `addTab(path)`, `moveTab(path, delta)`,
  `removeTab(path)` (the next tab becomes current, as in `QTabWidget`),
  `setTabTitle(path, title)`, `setSizes(path, sizes)`,
  `setCurrentTab(path, index)`.
* Uses QtCore's JSON only. "Frontend" because it describes the window's
  layout, but it is tested without widgets.

### `PanelLayoutView` (frontend; `layout/panellayoutview.{h,cpp}`)

* Built on the layout host (`layoutHost` in `mainwindow.ui`, inside
  `frame`), which it gives a layout for the root widget, on the store
  (`layoutElements`), and on the text of an empty slot.
* `addElement(id, title, widget)`, called by `MainWindow` for the built-in
  elements and each loaded plugin's panel, moves the widget to the store.
  `setUnavailableTitle(id, name)` names the plugins not loaded.
  `elementIds()`, `title(id)` and `automaticTabLabel(node)` (the label of
  an unnamed tab) serve the editor's menu.
* `setLayout(PanelLayout)` rebuilds the widgets as in "Rebuilding on each
  edit"; `layout()` is the current tree.
  * A placeholder is a centered label with that text; `MainWindow` gives
    "Empty. Turn on View > Edit Layout, then right-click here to insert a
    panel."
  * An unavailable element is a label "*Name* is not available".
* `pathOf(widget)` returns the path of the node a widget belongs to, and
  `widgetAt(path)` the widget of a node, for the editor.
* Emits `layoutChanged()` after a splitter move or a tab switch, with the
  change already in `layout()`. Knows nothing about edit mode.

### `PanelLayoutEditor` (frontend; `layout/panellayouteditor.{h,cpp}`)

* A child of the view. `setEnabled(bool)` installs or removes its event
  filter on the application.
* Builds the menu for a path, shows the overlay, applies the chosen edit to
  a copy of the view's layout, calls `setLayout`, and emits
  `layoutChanged()`.
* Holds the cut subtree.
* `renameTab(path)` shows the Rename Tab dialog (`QInputDialog::getText`).

### `MainWindowLayout` (app; `mainwindowlayout.{h,cpp}`)

* The app's side of the layout: the built-in element ids (`kPlaylist`,
  `kProperties`, `kSpectrum`, `kCover`) and `defaultLayout()`.

### `MainWindow`

* `setUpLayout`:
  1. Registers the built-in elements, and each loaded plugin's panel,
     created in `layoutElements`; names the plugins not loaded.
  2. Builds `layout/tree`, or the default when it is missing or invalid.
  3. Saves `layout/tree` on the view's and the editor's `layoutChanged`.
  4. Connects View > Edit Layout to the editor and View > Reset Layout... to
     a confirmation and the default.
* The Properties panel's buttons are connected where `SongInfoPresenter` is
  created.

## Edge cases

* **Startup:** the layout is built in the constructor, with `PlaylistTabs`
  placed before `finishSetUpPlaylist` initializes it.
* **The playlist left out of the layout:** described in the first key
  decision. Find, Open and the playback commands still act on the hidden
  current playlist.
* **A plugin disabled, then enabled:** its node shows "not available", then
  the panel again after the restart that enabling needs.
* **Cut, then another Cut:** the first subtree is dropped. Its elements are
  out of the tree and can be inserted again.
* **Delete or Replace on the root:** the root becomes a placeholder, or the
  new node. An element as the root is placed in the host directly.
* **Right-click with the Menu key:** it sends `QEvent::ContextMenu` too, to
  the focus widget, so it opens the same menu for that widget's node.

## Tests

* `tst_panellayout`:
  * JSON round trip, and the JSON written for a hand-made tree;
  * rejecting bad trees (not JSON, an array, unknown type, wrong child
    count, missing, negative or non-number sizes, tabs without children,
    `current` out of range, titles not one string per tab, element without
    id, element or placeholder with children, an invalid child);
  * repeated elements becoming placeholders;
  * each edit: replace on an element, a container and a placeholder; split
    and wrap in tabs keeping the node; take leaving a placeholder; paste
    with an element inserted since; adding, moving and removing tabs, with
    the current tab following;
  * tab names staying with their tabs through split, take, add, move and
    remove, kept in the JSON, and left out of it when no tab is named.
* `tst_panellayoutview` (offscreen):
  * building places each element in its splitter or tab, with the tab
    labels (a named tab shows its name), the placeholder label (the text the
    view was given) and the "not available" labels; an element not in the tree stays hidden in the
    store;
  * a rebuild keeps the element widgets alive and moves an element left out
    to the store, hidden, including an element that was the root;
  * splitter moves and tab switches are written to the tree and emit
    `layoutChanged`, and a rebuild emits nothing and keeps the current tab;
  * `pathOf` for a child of an element, a tab page, a splitter handle and a
    tab bar, and nothing for the store and the window.
* `tst_panellayouteditor` (offscreen):
  * a context-menu request opens the layout menu only in edit mode, with
    the overlay over the node until the menu closes; it reaches the
    widget's own menu otherwise, and a request outside the layout passes
    through;
  * the menu's items for an element, its parent splitter, a tab and a
    placeholder tab, and which are enabled;
  * each item without state of its own (Replace with an element, both
    splitters, Tabs, Delete, Move Tab Left and Right, Remove Tab, Add Tab)
    makes the same edit as its `PanelLayout` call, and reports it once;
  * Cut and Paste (the cut subtree is pasted once) and Rename Tab (the
    dialog shows the tab's label; renaming to that label unnames the tab).
* `tst_coverartpresenter`: a resize of the shown label rescales the cover.
* `tst_mainwindow`:
  * `layout_defaultAndSavedTabsAndShowSelectedSong`: the default layout as
    built (Properties above the cover in a vertical splitter, the playlist
    beside them, no tabs), Lyrics and Spectrum in the store; a saved
    layout's tabs and their order, and the current tab restored after
    restarting the window;
  * `layout_startsAtAQuarterAndRestoresDraggedSizes`: the quarter width,
    and both splitters' dragged sizes restored;
  * `layout_resetAndInvalidSavedLayoutGiveTheDefault`: a saved layout of
    the playlist alone, with Properties in the store; Reset Layout after
    its confirmation; an invalid key with its warning;
  * `layout_editModeIsWiredAndItsEditsAreSaved`: View > Edit Layout turns
    the layout menu on and off, and an edit from it is saved in
    `layout/tree`;
  * `plugins_disabledPluginsHaveNoPanelPageOrMenu`: a disabled plugin's
    node shown as not available and kept in `layout/tree`.
* By hand: edit mode on each platform; the overlay; that no widget's own menu
  opens in edit mode; the cover rescaling in a splitter and in a tab; the
  look of the default layout.

## Out of scope

* The playback buttons, seek bar and volume as elements, with a box
  container that does not stretch its children. That is DeaDBeeF's default
  layout (`playtb`, `seekbar`, `volumebar` in an `hbox`).
* Elements placed more than once; splitter lock modes; Copy; Undo; importing
  and exporting layout files (DeaDBeeF's `.ddblayout`).

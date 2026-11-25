# Custom layout editing

## Summary

* The main window's central widget holds two **layouts**, each a tree of
  containers whose leaves are **elements**:
  * the **toolbar**, on top: one row of the playback controls (Playback
    Buttons, Seek Bar, Volume), which stays while they are placed, moved
    and removed in it;
  * the **panel layout** below it: splitters and tabs of the panels
    (Playlist, Properties, Spectrum, Cover Art, and each plugin panel).

  A control goes only in the toolbar, and a panel only in the panel layout.
  Only the menu bar and the status bar are fixed.
* **View > Edit Layout** turns on edit mode for both. In it, right-clicking
  anywhere in a layout opens a menu for the clicked node instead of the
  widget's own menu: Insert or Replace with, Delete, Cut, Paste, the node's
  own items, and a Parent submenu. Left clicks still work normally. **View >
  Reset Layout...** goes back to both defaults.
* Each layout is saved as JSON in its own key, `layout/toolbar` and
  `layout/tree`, after every change, including splitter drags and tab
  switches.
* The default toolbar is a row of the buttons, the volume and the seek bar.
  The default panel layout is Properties above the cover on the left and the
  playlist on the right, with no tabs. Lyrics, Spectrum and any other plugin
  panel start outside it, to be inserted from edit mode.
* The model is DeaDBeeF's design mode (its GTK UI's `widgets.c`): a JSON tree
  of typed nodes, empty slots as their own node type, a node whose element is
  missing kept unchanged, and the same context menu. The differences, and
  why, are in "Key decisions".

## Key decisions

### An editable layout, not a fixed one

* Rejected: a fixed layout in `mainwindow.ui`: the controls above, and
  the info panels as tabs of one `QTabWidget` above the cover, beside the
  playlist. The app would decide every panel's place, so a plugin panel
  could only be added where the app put plugins (its tabs, before or after
  the built-in ones), and nothing could sit side by side with the playlist
  or the other panels.
* Cost: the layout module, edit mode and two saved trees, for what a fixed
  layout gets from `mainwindow.ui` alone.

### Elements are single, existing widgets that the layout places

Each element is one widget that `MainWindow` already has (`playlistTabs`,
`songInfoPanel`, `spectrumWidget`, `coverLabel`, `playbackButtons`,
`seekSlider`, `volumeSlider`, each plugin's panel from `createPanel`). The
layout only places it: building puts it in a container, and an element
that is not in its tree waits, hidden, in the hidden `layoutElements`
widget. Each element appears at most once.

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
  `layoutElements` and the layout hosts are descendants of `centralWidget`,
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

### Two layouts: the toolbar and the panel layout

| Layout | Host (`mainwindow.ui`) | Elements | Containers offered | Key |
| --- | --- | --- | --- | --- |
| Toolbar | `toolbarHost` | Playback Buttons, Seek Bar, Volume | none: one fixed row (an `hbox`) | `layout/toolbar` |
| Panel layout | `layoutHost`, in `frame` | Playlist, Properties, Spectrum, Cover Art, plugin panels | splitters, tabs | `layout/tree` |

Each is a `PanelLayoutView` with its own `PanelLayoutEditor`, tree, key and
default. Both views keep their elements in `layoutElements`; a view parks
and moves only the elements registered with it.

* **What may go where is decided by registration.** Each view registers only
  its own elements, so each editor's Insert and Replace with list only
  those. Cut and Paste stay within one layout: each editor holds its own cut
  subtree. Nothing checks ids against a list of allowed ones.
  * A hand-edited key naming the other layout's element shows "*id* is not
    available" (`PanelLayoutView::title` falls back to the id), the rule
    for any id the view doesn't have. Accepted: only a hand edit gets there,
    and the label shows it. Rejected: dropping such a node, or the whole
    key, which needs a check of its own for that one case.
* **The containers each editor offers** are a constructor argument of
  `PanelLayoutEditor`: splitters and tabs in the panel layout, none in the
  toolbar. `PanelLayout` and the view still accept every kind, so a
  hand-edited key with tabs in the toolbar is built as written.
* **The toolbar is one row that stays.** Its root is an `hbox`, which its
  editor fixes (`fixRoot`): the row's menu has no Replace with, Delete, Cut
  or Paste, only Add Slot. Its menus call the row "Toolbar"
  (`setContainerTitle`); "box" is the implementation, and the layout module
  keeps its own name, "Box (Left to Right)", for everywhere else. So the
  toolbar's menus edit only the controls in the row: Replace with (the
  controls not in it), Delete, Cut, Paste, Move Left and Right, Remove Slot.
  * Rejected: columns and nested rows in the toolbar (a `vbox` of the
    buttons and the volume beside the seek bar). A toolbar is one line of
    controls; a column doubles its height for two controls, and the menus
    would offer two more containers. Cost: the controls are always side by
    side.
  * Rejected: splitters and tabs in the toolbar. A handle or a tab bar has
    no use in a row of controls.
  * A hand-edited `layout/toolbar` whose root is not an `hbox` is built as
    written, with that root fixed; Reset Layout brings the row back.
* **The toolbar keeps its height:** `toolbarHost` has stretch 0 and `frame`
  1 in `centralWidget`'s layout. With a stretch set, `qGeomCalc` gives a
  stretch-0 item its size hint and the rest to the others, so the toolbar
  is as tall as its root's size hint (for a box, see "Boxes") and the panel
  layout takes the rest. `frame` draws the panels' raised border, not around the toolbar.
* Rejected: one tree holding the controls and the panels, as in DeaDBeeF
  (its default layout has `playtb`, `seekbar` and `volumebar` in an `hbox`
  above the playlist in a `vbox`). Keeping controls out of panel containers
  would need a per-element flag checked by every edit that moves a subtree
  (Paste, Replace with).
* Rejected: a `QToolBar`. It can hold the element widgets in a line
  (`addWidget`), but the row as a `PanelLayoutView` shares the panel
  layout's editor, storage and rebuild, which a `QToolBar` would need its
  own of. It also brings what edit mode would have to work around: dragging
  to other window edges, an overflow menu that hides what doesn't fit, and
  `QMainWindow`'s show-or-hide-toolbars context menu.
* Cost: a control can't sit beside a panel, for example the seek bar under
  the spectrum.

### The tree: splitters of two, boxes, tabs, elements, placeholders

| Node | JSON | Children |
| --- | --- | --- |
| Horizontal or vertical splitter | `{"type": "hsplitter" \| "vsplitter", "sizes": [a, b], "children": [...]}` | exactly 2 |
| Horizontal or vertical box | `{"type": "hbox" \| "vbox", "children": [...]}` | 1 or more |
| Tabs | `{"type": "tabs", "current": i, "titles": [...], "children": [...]}` | 1 or more |
| Element | `{"type": "element", "id": "playlist"}` | none |
| Placeholder (empty slot) | `{"type": "placeholder"}` | none |

* Built-in element ids are `playlist`, `properties`, `spectrum`, `cover`,
  `playbackbuttons`, `seekbar` and `volume` (`MainWindowLayout::kPlaylist`,
  ...). A plugin panel's id is its plugin's id (`lyrics`). The layout module
  does not know any of them: the app names the elements it registers.
* **Delete and Cut leave a placeholder** (as in DeaDBeeF), so a container
  never changes its number of children through them. A splitter always has
  two, and no edit has to collapse or merge containers. Three panels side by
  side with handles nest two splitters. A box or a tabs node changes its
  number of children only through its own items (Add and Remove).
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

### Boxes: rows and columns without handles

A box lines its children up, each at the size its widget asks for, with no
handles: the toolbar's row is one, where a splitter would let the buttons be
dragged wider. It is DeaDBeeF's `hbox` and `vbox` (`w_hvbox.c`). The layout
module has both directions, and boxes nest; the app uses the one row.

* Built as a `QWidget` with a `QBoxLayout`, left to right or top to bottom,
  no margins, the style's spacing. Each child is shown, as in a splitter.
* **What grows is the widget's size policy**, as `QBoxLayout` does: extra
  space goes to the children that expand (the seek bar, the volume up to
  its 110-pixel maximum), and the others keep their size hint. A box's
  widget counts as expanding when its layout is
  (`QWidgetItem::expandingDirections`).
  * DeaDBeeF's default too: `w_hvbox_append` turns Expand on unless the
    widget is `DDB_GTKUI_WIDGET_FLAG_NON_EXPANDABLE` (`playtb`).
  * Rejected: DeaDBeeF's per-child Expand and Fill and its Homogeneous
    toggle: three menu items and two JSON lists for what the policies
    decide. Cost: two expanding children share the space evenly.
* **A box is no larger than its children allow**: its layout's size
  constraint is `QLayout::SetMaximumSize`, so the box's widget takes the
  maximum its layout computes. Without it a box's widget, Preferred like
  any `QWidget`, would take a share of its parent's extra space. For
  example, a column of a fixed-width widget and a slider beside an
  expanding widget in a row expands sideways (through the slider) and would
  take half the row. With it the column's maximum width is its expanding
  child's maximum (`qMaxExpCalc`), raised to its minimum width where that
  is larger, and the expanding widget takes the rest. For the toolbar's
  row, the maximum height is the controls' height (the sliders' vertical
  policy is Fixed and the buttons are fixed size), so the row never grows
  taller than them.
* A placeholder in a box is the same word-wrapped label as elsewhere, so a
  new slot in the toolbar, or a control deleted from it, makes the toolbar
  a few lines taller until the slot is filled or removed. Accepted: a
  shorter text for slots in boxes would be a second placeholder kind for a
  passing state.
* Rejected: a spacing setting on the box node. Cost: a row's gaps are the
  style's box spacing.

### The playback controls as elements

| Id | Title | Widget (`mainwindow.ui`) | Grows |
| --- | --- | --- | --- |
| `playbackbuttons` | Playback Buttons | `playbackButtons`: Play, Pause, Stop, Previous, Next, Random, each 24 x 24 (`setIconOnlyPlaybackButton`) | No |
| `seekbar` | Seek Bar | `seekSlider` (`JumpSlider`) | Horizontally |
| `volume` | Volume | `volumeSlider` | Horizontally, from 84 up to 110 |

* They are elements like the panels (the first key decision): single
  widgets that `MainWindow` connects once, declared in `layoutElements`,
  and registered with the toolbar in this order, their order in its Insert
  menu.
* `playbackButtons` is a plain widget whose layout is
  `playbackButtonsLayout`, so the buttons keep their 6-pixel spacing
  ([ui-styling.md](ui-styling.md)). Its size policy is Fixed, so a box
  never gives it more than its size hint: without the grow flag,
  `qSmartMaxSize` caps it there. With the default Preferred, a row whose
  expanding children are at their maximum, for example the buttons and the
  volume without the seek bar, would widen it, and its layout would spread
  the fixed-size buttons apart.
* The six buttons are one element. Rejected: one element per button. The
  menu would list six entries instead of one, and the default row would
  have eight slots instead of three. Cost: the buttons can't be reordered
  or left out one by one.
* A control left out still works from elsewhere: the Playback menu has
  every button's action, and the volume keeps its saved value.

### Storage: a key per layout, the defaults built in code

* `layout/toolbar` and `layout/tree` hold the trees as compact JSON
  (`PanelLayout::toJson`). Each is written after every edit, splitter move
  and tab switch in its layout.
* A missing key gives its layout's default. A key that does not parse, or
  breaks the table above (unknown type, wrong child count, missing or
  negative sizes, `current` out of range, `titles` not one string per tab),
  also gives the default, with a `qWarning`. The key is user data from
  outside the program, so it is checked once, at load
  (`PanelLayout::fromJson`). Inside the program the tree is valid by
  construction, and the edit operations do not re-check it.
* An element id that appears twice (in a hand-edited key) is kept the first
  time, in tree order; later copies become placeholders. Paste applies the
  same rule (see "Edit mode").
* The defaults are the app's (`MainWindowLayout`):
  * `defaultToolbar()`: an `hbox` of `playbackbuttons`, `volume` and
    `seekbar`, the seek bar taking the rest of the width;
  * `defaultPanels()`: `hsplitter [320, 960]` with `vsplitter [300, 500]`
    (3 to 5) of `properties` and `cover` on the left and `playlist` on the
    right. The info side starts at a quarter of the width (the sizes scaled
    to the splitter), so its width does not depend on the panels' size
    hints. Rejected: the splitter's sizing by size hint, which starts the
    side too narrow, since it uses a hint cached before the plugin panels
    are placed (Qt does not pass a hidden widget's hint change up to that
    cache), and clearing those caches by hand, which depends on Qt's
    internals. Cost: that first width ignores what the panels' content
    needs; the panes' minimum widths still hold.

  The panel layout has no tabs and holds none of the plugins' panels, so it
  is the same whatever plugins are loaded. `spectrum` and the plugin panels
  (Lyrics) start in the store. Cost: Lyrics and the spectrum are not shown
  until the user inserts them in edit mode.
* An incompatible format change later gets a new key, as DeaDBeeF's
  `gtkui.layout.<version>` does, rather than a version field and migration
  code now.
* A plugin's panel, like any element not in the tree, is shown only once
  the user inserts it. Both reference players behave the same way. Adding
  it automatically would mean guessing where it belongs.

### Edit mode

* View > Edit Layout is a checkable action that turns both editors on and
  off. It is not saved, so the app always starts with it off.
* While it is on, each `PanelLayoutEditor` is an event filter on the
  application. It takes every `QEvent::ContextMenu` whose receiver is in its
  layout, so a
  widget's own menu (playlist rows, tabs, header, lyrics) does not open. The
  node is found by walking from the receiver up its parents to the first
  widget the view built or placed (`PanelLayoutView::pathOf`), so a click on
  a splitter handle or a tab bar reaches its container. Other events, and
  requests outside its layout (the other layout, dialogs, the menu itself),
  pass through.
* A translucent overlay over the node's widget, a child of the window that
  ignores the mouse, shows which node the menu is for while the menu is
  open. Rejected: DeaDBeeF hides the node's children to paint the highlight,
  which in Qt would change the layout under the menu.
* The menu for a node:
  1. its title, disabled;
  2. **Insert** (on a placeholder) or **Replace with** (otherwise): the
     layout's elements not in its tree, then its containers (in the panel
     layout Splitter (Left and Right), Splitter (Top and Bottom) and Tabs;
     none in the toolbar). It is disabled when it has nothing to offer: in
     the toolbar, once every control is in the row. A new
     splitter's or box's first child is the replaced node and its second is
     a placeholder; DeaDBeeF starts a box with three placeholders instead.
     A new tabs node holds the replaced node. On a placeholder, a new
     container holds placeholders. Replacing a container with an element
     drops its subtree: its elements leave the tree;
  3. **Delete** and **Cut** (not on a placeholder): replace the node with a
     placeholder. Cut keeps the subtree to paste;
  4. **Paste** (enabled when something was cut): replaces the node with the
     cut subtree. Any element in it that has been inserted elsewhere since
     becomes a placeholder (the same rule as at load). The cut subtree is
     then cleared;
  5. for a tabs node: **Add Tab**; for a box: **Add Slot** (each appends a
     placeholder);
  6. for a tab of a tabs node: **Move Tab Left**, **Move Tab Right**,
     **Rename Tab...** and **Remove Tab**; for a child of a box: **Move
     Left** and **Move Right** (`hbox`) or **Move Up** and **Move Down**
     (`vbox`), and **Remove Slot**. Each is disabled where it can't apply
     (first, last, only child). Rename Tab asks for a name, prefilled with
     the tab's label. An empty name, or one equal to the label the tab would
     have unnamed, makes it unnamed again. Rejected: DeaDBeeF's Expand and
     Shrink Box by 1 Item (removing the last child) with no moves, which
     reorder a row only by Cut and Paste through a placeholder; ours mirror
     the tab items;
  7. **Parent**: the parent's menu, as a submenu, when the node has a parent.
     It reaches a container whose children cover it, as in DeaDBeeF.

  Items 2 to 4 are left out for a fixed root: the toolbar's menu is
  "Toolbar" and Add Slot.
* The menu is opened with `popup()`, not `exec()`, so the event filter
  returns at once. The chosen action runs from the menu's own event, never
  while an event is being delivered to a layout widget that the action's
  rebuild deletes. The menu is deleted from the event loop after it hides
  (`aboutToHide` comes before the action triggers). Rename Tab's dialog,
  which runs an event loop of its own, is opened from the event loop too
  (a queued call), after the menu's action has returned.
* The cut subtree is kept in memory only, not on the clipboard.
* Reset Layout asks for confirmation once, then replaces both trees with
  their defaults and saves them. Rejected: a reset per layout, two menu
  items for a rarely used command.
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
2. Move every element the view placed back to `layoutElements`, hidden
   explicitly; the other layout's elements are not touched.
3. Delete the old root widget, unless it is an element (then it was moved
   in step 2).
4. Build new containers, place the elements, and apply each node's sizes and
   current tab.
5. Only then connect the new containers' signals.

* The order of steps 1 and 5 matters. Taking pages out of a `QTabWidget`,
  or adding its first page, emits `currentChanged`. Connected at those
  times, it would write a stale or zero index into the tree.
* Moving the elements out first means deleting the containers never deletes
  an element. A `QSplitter`, a box's widget or a `QTabWidget` deletes its
  children. They are hidden explicitly so a widget that follows its
  visibility (the spectrum) sees the hide, and each element placed in a
  splitter or a box is shown again; a tab widget shows and hides its pages
  itself.
* The layout has a handful of widgets, so a full rebuild costs nothing
  noticeable. It also needs no per-container insert, remove and replace
  code (DeaDBeeF's `append`/`remove`/`replace` callbacks).
* Splitter sizes and the current tab are written into the tree as they
  change (`splitterMoved`, `currentChanged`), so the tree is always current
  and a rebuild loses nothing.
* The view is a `QObject` child of its host, created before the containers
  it builds there, so it is destroyed before them: their signals during the
  window's destruction reach nothing.

### Cover art follows its label

`CoverArtPresenter` rescales the cover on each of its label's resizes, so
it follows the label in any splitter or tab, and when a hidden label is
shown (Qt delivers the pending resize then); see
[info-panel.md](info-panel.md), "`CoverArtPresenter`".

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
* Edits: `replaceWithElement(path, id)`, `split(path, kind)` (a splitter or
  a box), `wrapInTabs(path)`, `take(path)` (Delete and Cut; leaves a
  placeholder), `paste(path, subtree)`, `addChild(path)`,
  `moveChild(path, delta)`, `removeChild(path)`, `setTabTitle(path, title)`,
  `setSizes(path, sizes)`, `setCurrentTab(path, index)`.
  * `addChild`, `moveChild` and `removeChild` act on a box's or a tabs
    node's children. For tabs they keep `titles` and `current` in step: a
    title moves with its tab, the current tab stays current, and removing
    it makes the next one current, as in `QTabWidget`. Rejected: separate
    box and tab operations, which would repeat the same vector edits.
* Uses QtCore's JSON only. "Frontend" because it describes the window's
  layout, but it is tested without widgets.

### `PanelLayoutView` (frontend; `layout/panellayoutview.{h,cpp}`)

* Built on a host (`toolbarHost` or `layoutHost` in `mainwindow.ui`), which
  it gives a layout for the root widget, on the store (`layoutElements`),
  and on the text of an empty slot.
* `addElement(id, title, widget)`, called by `MainWindow` for the built-in
  elements and each loaded plugin's panel, moves the widget to the store.
  `setUnavailableTitle(id, name)` names the plugins not loaded.
  `elementIds()`, `title(id)` and `automaticTabLabel(node)` (the label of
  an unnamed tab) serve the editor's menu.
* `setLayout(PanelLayout)` rebuilds the widgets as in "Rebuilding on each
  edit"; `layout()` is the current tree.
  * A placeholder is a centered label with that text; `MainWindow` gives
    "Empty. Turn on View > Edit Layout, then right-click here to insert a
    panel." (a control, in the toolbar).
  * An unavailable element is a label "*Name* is not available".
  * A box is a plain widget with a box layout (see "Boxes").
* `pathOf(widget)` returns the path of the node a widget belongs to, and
  `widgetAt(path)` the widget of a node, for the editor.
* Emits `layoutChanged()` after a splitter move or a tab switch, with the
  change already in `layout()`. Knows nothing about edit mode.

### `PanelLayoutEditor` (frontend; `layout/panellayouteditor.{h,cpp}`)

* A child of the view, given the container kinds its Insert and Replace
  with offer. `setContainerTitle(kind, title)` names a kind in its menus in
  place of the default name; `fixRoot()` keeps the root from being
  replaced, deleted, cut or pasted over. `setEnabled(bool)` installs or
  removes its event filter on the application.
* Builds the menu for a path, shows the overlay, applies the chosen edit to
  a copy of the view's layout, calls `setLayout`, and emits
  `layoutChanged()`.
* Holds the cut subtree.
* `renameTab(path)` shows the Rename Tab dialog (`QInputDialog::getText`).

### `MainWindowLayout` (app; `mainwindowlayout.{h,cpp}`)

* The app's side of the layouts: the built-in element ids (`kPlaylist`,
  `kProperties`, `kSpectrum`, `kCover`, `kPlaybackButtons`, `kSeekBar`,
  `kVolume`), `defaultToolbar()` and `defaultPanels()`.

### `MainWindow`

* `setUpLayout`:
  1. Registers the controls with the toolbar's view, and the built-in
     panels and each loaded plugin's panel, created in `layoutElements`,
     with the panel layout's; names the plugins not loaded.
  2. Creates each layout's editor with its container kinds (for the
     toolbar none, its row fixed and named "Toolbar"), then calls
     `setUpLayoutView` for each: it builds the key, or the default when the
     key is missing or invalid; saves the key on the view's and the
     editor's `layoutChanged`; connects View > Edit Layout to the editor;
     and returns the layout's reset.
  3. Connects View > Reset Layout... to a confirmation and both resets.
* The Properties panel's buttons are connected where `SongInfoPresenter` is
  created. The controls are connected in `setUpPlaybackActions` and
  `initVolumeControl`, wherever they are placed.

## Edge cases

* **Startup:** the layout is built in the constructor, with `PlaylistTabs`
  placed before `finishSetUpPlaylist` initializes it.
* **The playlist left out of the panel layout:** described in the first key
  decision. Find, Open and the playback commands still act on the hidden
  current playlist.
* **A control left out of the toolbar:** the Playback menu still plays,
  pauses and skips; a hidden seek slider and volume slider keep their
  values, and playback keeps updating the seek slider.
* **A plugin disabled, then enabled:** its node shows "not available", then
  the panel again after the restart that enabling needs.
* **Cut, then another Cut:** the first subtree is dropped. Its elements are
  out of the tree and can be inserted again.
* **Delete or Replace on the panel layout's root:** the root becomes a
  placeholder, or the new node. An element as the root is placed in the
  host directly. The toolbar's root offers neither.
* **Every control removed from the toolbar:** the row stays with its
  slots; Remove Slot is disabled on the last one, so the row is never
  empty.
* **Right-click with the Menu key:** it sends `QEvent::ContextMenu` too, to
  the focus widget, so it opens the same menu for that widget's node.

## Tests

* `tst_panellayout`: the JSON (round trip, each rejected tree, repeated
  elements) and each edit, with tab names and the current tab following.
* `tst_panellayoutview` (offscreen): building (elements, boxes, labels, the
  store), rebuilding (elements kept and parked), the tree following
  splitter moves and tab switches, and `pathOf`.
* `tst_panellayouteditor` (offscreen): edit mode and the overlay, each
  node's menu (the container kinds and names given, a fixed root, Replace
  with disabled when empty), each item's edit, Cut and Paste, and Rename
  Tab.
* `tst_coverartpresenter`: the cover rescaling on a resize.
* `tst_mainwindow`, the `layout_*` tests: both defaults as built, saved
  layouts and the current tab restored, the quarter width and dragged
  sizes, Reset Layout and an invalid key, and edit mode in both layouts
  (each offering only its own elements, the toolbar's row menu). Also a
  disabled plugin's node kept, and the controls' tests with the controls
  placed by the toolbar.
* By hand: edit mode in both layouts on each platform; the overlay; that no
  widget's own menu opens in edit mode; the cover rescaling in a splitter
  and in a tab; the look of the default toolbar (the row's spacing, the
  buttons' and volume's widths, the seek bar filling the rest) and the
  panels' frame below it; the toolbar's menus (the row titled "Toolbar",
  with Add Slot only); seeking and volume after moving the controls.

## Out of scope

* Elements placed more than once; splitter lock modes; Copy; Undo; importing
  and exporting layout files (DeaDBeeF's `.ddblayout`).
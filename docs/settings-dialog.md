# Plan: Settings dialog navigation tree

Status: implemented.

`SettingsDialog` (Preferences) shows its pages in a **navigation tree**, so
a page can have child pages: the Plugins page and, under it, each loaded
plugin's settings page ([plugins.md](plugins.md)). Each page's contents are
described in its feature's doc (see "Pages").

## Layout

* A tree on the left (`settings_page_tree`, no header) and a
  `QStackedWidget` on the right (`settings_pages`), in a `QSplitter`
  (`settings_splitter`). Each item holds its page; selecting it shows the
  page. The pages from the `.ui` (`playback_tab`, `fields_tab`,
  `cloud_sync_tab`) are in the stack from the start; `setUpPageTree` adds
  the pages built in code and the items.
* The items, in this order: Playback, Library, Fields, Display, Cloud
  Sync, Plugins (see "Pages").
* When the dialog is built, the tree is as wide as its longest item, at
  least 150 px and at most a third of the dialog; a longer title is elided,
  with the whole title as the item's tooltip, and the user can drag the
  splitter. The width is not stored: the next dialog fits its items again,
  and a stored width would be one more setting for what fitting already
  does. Neither side of the splitter collapses.
* A page has no title of its own: the selected item names it.
* The stack's size hint and minimum size are its largest page's (as
  `QStackedLayout` computes them), so every page fits the dialog, and
  switching pages does not resize it. The `.ui`'s starting size (560 × 760)
  is widened by the tree's width, so the pages keep that width; a later
  change of the dialog's width goes to the pages.
* The dialog opens on Playback.
* OK (`accepted`) runs `applySettings`, which applies Playback, the plugin
  pages, the Plugins page, Display, Cloud Sync and Fields. Library's
  actions apply at once, not on OK.

## Pages

| Item | Page | Doc |
| --- | --- | --- |
| Playback | The playback backend and ReplayGain (`.ui`) | [playback.md](playback.md) |
| Library | `LibraryFoldersPage` | [monitored-library.md](monitored-library.md) |
| Fields | Custom tag and computed fields (`.ui`) | [architecture.md](architecture.md), "Column and layout metadata" |
| Display | Theme, status bar and window title expressions (built in code) | [expressions.md](expressions.md), [ui-styling.md](ui-styling.md) |
| Cloud Sync | User UUID (`.ui`) | [cloud-sync.md](cloud-sync.md) |
| Plugins | `PluginsPage`; a child per loaded plugin with a settings page, in load order, titled with its `settingsPageTitle()`; expanded | [plugins.md](plugins.md) |

* A new built-in page is a new top-level item, before Plugins.

## Rejected

* **Tabs, with a Plugins tab holding nested tabs or a plugin picker.** Tabs
  inside tabs do not scale; a picker hides each plugin's settings one step
  deeper. A plugin's page as a tab of its own mixes plugins in among the
  built-in pages.
* **A fixed tree width.** A long plugin page title would be cut off with no
  way to read it.
* **A tree as wide as any title.** A very long title would leave the pages
  little room.
* **Storing the splitter's width** (see "Layout").

## Costs

* The dialog is wider by the tree's width (at least 150 px), so the pages
  keep their width; tabs cost a row of height instead.

## Tests

* `tst_settingsdialog_ui::pageTree_listsPagesAndShowsTheSelectedOne`: the
  top-level items and their order, a plugin's page under Plugins (and none
  for a plugin without one), the dialog opening on Playback, and that
  selecting an item shows its page (a `.ui` page, one built in code, the
  Plugins page and a plugin's page).
* `tst_mainwindow::settings_pluginPageAppliesToItsPanelOnOk` finds the
  Lyrics page under Plugins.
* By hand: the tree's width fits its items, a long title elided with its
  tooltip, dragging the splitter, and that switching pages does not resize
  the dialog.

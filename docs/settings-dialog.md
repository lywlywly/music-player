# Plan: Settings dialog navigation tree

Status: planned.

## Summary

* `SettingsDialog` (Preferences) shows its pages in a **navigation tree**
  instead of tabs, so a page can have child pages: the Plugins page and,
  under it, each plugin's settings page ([plugins.md](plugins.md), "Plan:
  Plugin management").
* The pages' contents do not change; each is described in its feature's
  doc (see "Pages").

## Layout

* A tree on the left (`settings_page_tree`, no header) and a
  `QStackedWidget` on the right (`settings_pages`) replace
  `settings_tab_widget`. Selecting an item shows its page. The built-in
  pages keep their widgets and object names; their tabs become pages of the
  stack.
* The items, in this order: Playback, Library, Fields, Display, Cloud
  Sync, Plugins (see "Pages"). The tabs today are Playback, Library,
  Fields, Cloud Sync, Lyrics, Display.
* The tree and the stack are in a `QSplitter`, since a plugin's page title
  can be of any length. When the dialog is built, the tree is as wide as
  its longest item, at least about 150 px and at most a third of the
  dialog; a longer title is elided, with the whole title as the item's
  tooltip, and the user can drag the tree wider. The width is not stored:
  the next dialog fits its items again, and a stored width would be one
  more setting for what fitting already does.
* A page has no title of its own: the selected item in the tree names it.
* The stack's size hint and minimum size are its largest page's, so every
  page fits the dialog, and switching pages does not resize it. The .ui's
  starting size (560 × 760) is widened by the tree's width.
* The dialog opens on Playback, as it opens on its first tab today.
* OK (`accepted`) runs `applySettings`, which applies Playback, the plugin
  pages, the Plugins page, Display, Cloud Sync and Fields. Library's
  actions apply at once, not on OK.

## Pages

| Item | Page | Doc |
| --- | --- | --- |
| Playback | The playback backend (`.ui`) | [playback.md](playback.md) |
| Library | `LibraryFoldersPage` | [monitored-library.md](monitored-library.md) |
| Fields | Custom tag and computed fields (`.ui`) | [architecture.md](architecture.md), "Column and layout metadata" |
| Display | Theme, status bar and window title expressions (built in code) | [expressions.md](expressions.md), [ui-styling.md](ui-styling.md) |
| Cloud Sync | User UUID (`.ui`) | [cloud-sync.md](cloud-sync.md) |
| Plugins | `PluginsPage`; a child per loaded plugin with a settings page, in load order, titled with its `settingsPageTitle()`; expanded | [plugins.md](plugins.md) |

* A new built-in page is a new top-level item, before Plugins.

## Rejected

* **Keeping the tabs, with a Plugins tab holding nested tabs or a plugin
  picker.** Tabs inside tabs do not scale; a picker hides each plugin's
  settings one step deeper. A plugin's page as a tab of its own, as today,
  mixes plugins in among the built-in pages.
* **A fixed tree width.** A long plugin page title would be cut off with no
  way to read it.
* **A tree as wide as any title.** A very long title would leave the pages
  little room.
* **Storing the splitter's width** (see "Layout").

## Costs

* The dialog is wider by the tree's width (at least about 150 px), so the
  pages keep their width; tabs cost a row of height instead.

## Tests

* `tst_settingsdialog_ui`: the tree's items and their order; selecting one
  shows its page.
* `tst_mainwindow::settings_pluginPageAppliesToItsPanelOnOk` finds the
  Lyrics page under Plugins, instead of as a tab before Display.
* By hand: the tree's width fits its items, a long title elided with its
  tooltip, dragging the splitter, and that switching pages does not resize
  the dialog.

## Phases

1. **Tree.** The tree, splitter and stack in `SettingsDialog`; plugin pages
   under Plugins. `PluginsPage` comes with plugin management
   ([plugins.md](plugins.md)); until then the Plugins item shows an empty
   page.
2. **Docs.** This plan becomes the doc of the dialog in use.

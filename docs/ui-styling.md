# UI Styling

The playback toolbar's icons, the Windows application font and its CJK
fallback, the Windows table selection fix, and the light and dark theme.

## Playback toolbar icons

* `mainwindow.ui` keeps the playback buttons in a nested
  `playbackButtonsLayout`, so the group has its own compact spacing before the
  volume and seek sliders.
* The SVG icons are in `src/resources/icons`, registered through
  `src/resources/resources.qrc`; their attribution is in
  `THIRD_PARTY_NOTICES.md`. `media-playback-random.svg` is our own composite:
  a play triangle on the left and a question mark on the right.
* `MainWindow` calls `Q_INIT_RESOURCE(resources)`, because the qrc is compiled
  into the static library `MYLib`.
* The buttons are fixed-size, icon-only `QToolButton`s
  (`setIconOnlyPlaybackButton`, `playbackbuttonstyle.cpp`), tinted at runtime:
  `#fcfcfc` in dark mode, `#232629` otherwise. `MainWindow` re-tints them when
  the theme changes.
* The tinted pixmaps are rendered at the displaying widget's device pixel
  ratio, so they are sharp on high-DPI screens, while button and icon sizes
  stay in logical pixels. The tint fills the logical icon rect after
  `setDevicePixelRatio`, so it paints in the same coordinates as the icon.

## Application font and CJK fallback (Windows, `main.cpp`)

* Without an explicit family list, Qt does not use Windows font fallback
  (neither the registry font-link list nor DirectWrite system fallback). It
  uses a built-in list chosen by user language
  (`QWindowsFontDatabaseBase::extraTryFontsForFamily`); for non-CJK locales
  that renders Chinese text in MS UI Gothic / SimSun.
* Temporary fix: the app font uses the system UI family, then `Meiryo UI`,
  `Microsoft JhengHei UI`, `Microsoft YaHei UI`, the order of the `Segoe UI`
  entry in `HKLM\SOFTWARE\Microsoft\Windows NT\CurrentVersion\FontLink\SystemLink`
  on an en-US install. The lyrics font starts from it, so these families
  apply there too ([plugins.md](plugins.md)).
* Limitation: Han characters shared with Japanese use Meiryo UI glyph shapes,
  and Simplified-only characters fall through to JhengHei / YaHei, so one line
  of Chinese can mix fonts (native Windows does the same on non-Chinese
  locales). Possible replacements: choose the CJK family order by detected
  text language, or use one CJK family with consistent design across
  languages (for example Source Han Sans).
* Size: native Windows draws CJK fallback fonts larger than the base font
  (Segoe UI 9pt falls back to Meiryo UI 10pt). This is hard in Qt:
  `QFontEngineMulti` creates every fallback engine at the base font size, so
  it would need per-run layout in every place text is drawn (custom
  delegates, `QProxyStyle` overrides for menus). Instead the whole app font is
  set to 10pt on Windows.
* 10pt also matches the `windows11` style's menu bar, which paints
  `CE_MenuBarItem` text at a hard-coded 10pt while sizing items from the
  widget font. With another app font size, menu bar gaps change but the text
  does not. Other sizes would need a `QProxyStyle` override of
  `CE_MenuBarItem` or the `windowsvista` style.

## Table row selection (Windows, `tableselectionstyle.cpp`, installed in `main.cpp`)

* Symptom: in a row-selecting `QTableView`, the `windows11` style draws a
  selected row as separate rounded boxes, one per cell, each with its own
  accent-colored bar at the left edge.
* Cause, in the Qt style (`qwindows11style.cpp`), not in the app:
  * `QTableView` calls `PE_PanelItemViewRow` for every cell
    (`QTableViewPrivate::drawCell`). Since Qt 6.10 the style draws the
    tree/list accent bar there for any selected item, with no check of the
    view type; a code comment assumes only `QTreeView` calls it. Qt 6.8 drew
    one bar per row (on the `Beginning` cell only) and 6.9 drew none.
  * `CE_ItemViewItem` forces `onlyOne = true` for any `QTableView` (seen in
    6.11; 6.10 unconfirmed), ignoring the `viewItemPosition` that `QTableView`
    already sets, so each cell gets its own fully rounded box. 6.8 and 6.9
    used `viewItemPosition` and drew one continuous band.
  * Both are still present on the Qt `dev` branch (checked 2026-11) and not
    reported on bugreports.qt.io (searched 2026-11). The bar is the WinUI
    selection indicator added for `QTreeView` (see QTBUG-140494); tables get
    it because they use the same primitive.
* Fix: `TableSelectionStyle`, a `QProxyStyle` over `windows11`, set as the
  application style when the default style is `windows11`. For row-selecting
  tables only, it
  * draws `PE_PanelItemViewRow` with `State_Selected` removed, which keeps the
    alternate-row background but drops the bar;
  * paints the selection band itself in `CE_ItemViewItem`, rounded only at
    the row's ends as given by `viewItemPosition`, then lets the base style
    draw the content with the selected and hover state removed.
* The band is a neutral gray at 14% opacity (`kFillAlpha`): white in dark
  themes, black in light ones. The base style's subtle gray (WinUI
  `fillSubtleSecondary`, 6% / 4%) is hard to see. Its solid accent for
  alternating rows is not copied either: it leaves dark text on a dark accent
  (QTBUG-144903, QTBUG-141485), and no table uses alternating rows. On
  alternating rows the gray band is harder to tell from the alternate
  background.
* `viewItemPosition` counts hidden columns, so a row whose first or last
  column is hidden would lose its rounded end; no table hides columns.
* It does nothing in a high-contrast theme (which the base style detects as
  `Qt::ColorScheme::Unknown` and draws no bar for). Its 4px radius and 2px
  margins copy private values of the base style, so recheck them when
  upgrading Qt.
* Rejected: a `QStyledItemDelegate`. The bar is painted by the view before the
  delegate runs, and the style ignores `viewItemPosition` for tables.
* Removal: once a Qt release paints table rows correctly, delete
  `tableselectionstyle.{h,cpp}` and the `setStyle` block in `main.cpp`.

## Display theme (`displaytheme.cpp`, used by `SettingsDialog` and `MainWindow`)

* Stored as `display/theme_mode`: `system`, `light` or `dark`.
* `applyDisplayThemeMode` sets `QStyleHints::colorScheme`; the widget style
  stays the default one (on Windows, `windows11` wrapped by
  `TableSelectionStyle`).

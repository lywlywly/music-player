# Design docs

The design of the app, for contributors. [architecture.md](architecture.md)
holds the design shared by the whole app and links each topic doc; a topic doc
holds one feature's design. Progress and known issues are in
[roadmap.md](../roadmap.md); the user guide and build steps in
[README.md](../README.md). Explanatory guides, for people new to a
subsystem, are in [../guides/](../guides); they link to these docs, and
neither the code nor the docs reference them.

| Doc | Contents | Read before changing |
| --- | --- | --- |
| [architecture.md](architecture.md) | Design principles, main classes and responsibilities, startup, ingest, storage, threading | The design or the code structure |
| [playback.md](playback.md) | Playback queue, policies, skipping, seeking, audio backends, ReplayGain, OS media controls, GStreamer's bus messages and their threads, the GStreamer plugin scanner on Windows | Playback, the backends or system media |
| [expressions.md](expressions.md) | The expression language: song filters, status bar and window title | Search, autoplaylists or display expressions |
| [monitored-library.md](monitored-library.md) | Watched folders, scans, the file system watcher, autoplaylists, Playlist Search | The library or its watcher |
| [info-panel.md](info-panel.md) | The info panels, the Properties panel, the cover art | The info panels |
| [spectrum.md](spectrum.md) | The Spectrum panel, the `src/spectrum/` module, the players' audio taps | The spectrum or the players' audio path |
| [plugins/plugin-system.md](plugins/plugin-system.md) | The plugin system: plugins as DLLs with capabilities, the plugin API and its boundary, building and deploying a plugin, plugin metadata, versions, the host (`PluginManager`, `PluginPlayerHost`), disabling and the Plugins page, plugin commands | The plugin API, the host, the Plugins page, or before writing a plugin |
| [plugins/lyrics.md](plugins/lyrics.md) | The Lyrics plugin: its classes, where lyrics come from, its settings, playing from a clicked line, the hover highlight and the scroll hold | The Lyrics plugin |
| [plugins/abrepeat.md](plugins/abrepeat.md) | The A-B Repeat plugin: its menu, its classes, its precision | The A-B Repeat plugin |
| [plugins/playstats.md](plugins/playstats.md) | The Play Stats plugin: counting plays, its own database, the identity key; plugin-provided song fields (`ISongFieldProvider`) and how `SongLibrary` keeps and refreshes them; the startup load beside it; the threading contract; the migration | Play stats, cloud sync's local side, or plugin-provided fields |
| [event-ordering.md](event-ordering.md) | The order of deliveries, commands and playback events: re-entry in synchronous signals and listener calls, races (with their timing), plugin commands and their contract, the plan (a synchronous core with queued edges, the playback generation), how foobar2000 and DeaDBeeF handle it | A signal handler or listener that calls back into playback, a playback command, or the end-of-song handling |
| [layout-editing.md](layout-editing.md) | The main window's two editable layouts, the toolbar (playback controls in boxes) and the panel layout: elements, the layout tree and its JSON, boxes, the defaults, edit mode, rebuilding, the cover following its label | The main window's layout, the info panel's or the controls' placement, or an element |
| [settings-dialog.md](settings-dialog.md) | The Settings dialog's navigation tree, its pages and the docs that describe them | The Settings dialog's layout or a new page |
| [ui-styling.md](ui-styling.md) | Toolbar icons, the Windows font and CJK fallback, the Windows table selection fix, the theme | Icons, fonts, styles or the theme |
| [cloud-sync.md](cloud-sync.md) | Play-count sync, in the Play Stats plugin: the coordinator, cursor and rebase, DynamoDB model, Lambda API (`infra/aws/lambda`) | Cloud sync |

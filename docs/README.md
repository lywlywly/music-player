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
| [playback.md](playback.md) | Playback queue, policies, skipping, play counting, audio backends, ReplayGain, OS media controls, GStreamer's bus messages and their threads, the GStreamer plugin scanner on Windows | Playback, the backends or system media |
| [expressions.md](expressions.md) | The expression language: song filters, status bar and window title | Search, autoplaylists or display expressions |
| [monitored-library.md](monitored-library.md) | Watched folders, scans, the file system watcher, autoplaylists, Playlist Search | The library or its watcher |
| [info-panel.md](info-panel.md) | The info panel's tabs, the Properties tab, the cover art | The info panel |
| [spectrum.md](spectrum.md) | The Spectrum tab, the `src/spectrum/` module, the players' audio taps | The spectrum or the players' audio path |
| [plugins.md](plugins.md) | Plugins (DLLs with capabilities), the plugin API and its boundary, plugin metadata, disabling and the Plugins page, plugin commands, the Lyrics and A-B Repeat plugins | The plugin API, a plugin, the Plugins page, lyrics or A-B Repeat |
| [event-ordering.md](event-ordering.md) | The order of deliveries, commands and playback events: re-entry in synchronous signals and listener calls, races (with their timing), plugin commands and their contract, the plan (a synchronous core with queued edges, the playback generation), how foobar2000 and DeaDBeeF handle it | A signal handler or listener that calls back into playback, a playback command, or the end-of-song handling |
| [settings-dialog.md](settings-dialog.md) | The Settings dialog's navigation tree, its pages and the docs that describe them | The Settings dialog's layout or a new page |
| [ui-styling.md](ui-styling.md) | Toolbar icons, the Windows font and CJK fallback, the Windows table selection fix, the theme | Icons, fonts, styles or the theme |
| [cloud-sync.md](cloud-sync.md) | Play-count sync: DynamoDB model, Lambda API (`infra/aws/lambda`) | Play stats or sync |

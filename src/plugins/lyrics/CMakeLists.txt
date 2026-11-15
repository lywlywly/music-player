# The lyrics plugin (docs/plugins.md). Depends on Qt and the plugin API only,
# not on the app.

# The plugin's code: the Lyrics panel, its presenter, loading lyrics, and the
# settings page. A library of its own so the tests link it directly.
add_library(mylyrics STATIC
  lyricsloader.h
  lyricsloader.cpp
  lyricsmanager.h
  lyricsmanager.cpp
  lyricspanel.ui
  lyricspanel.h
  lyricspanel.cpp
  lyricspresenter.h
  lyricspresenter.cpp
  lyricssettings.h
  lyricssettings.cpp
  lyricssettingspage.h
  lyricssettingspage.cpp
)
target_link_libraries(mylyrics PUBLIC Qt6::Widgets pluginapi)
# Linked into a shared library.
set_target_properties(mylyrics PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The plugin the app loads: the root object.
add_library(lyricsplugin MODULE
  lyricsplugin.h
  lyricsplugin.cpp
  # Embedded by moc (Q_PLUGIN_METADATA); listed for IDEs.
  lyricsplugin.json
)
target_link_libraries(lyricsplugin PRIVATE mylyrics)
myplayer_deploy_plugin(lyricsplugin)

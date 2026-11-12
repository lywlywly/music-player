# The lyrics plugin's code: the Lyrics panel, its presenter, and loading
# lyrics. Depends on Qt and the plugin API only, not on the app; see
# docs/plugins.md.
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
)
target_link_libraries(mylyrics PUBLIC Qt6::Widgets pluginapi)

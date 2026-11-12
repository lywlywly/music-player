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

# The plugin the app loads: the root object. Built into the folder the app
# loads plugins from (plugins/ next to EXEC, the macOS bundle's PlugIns).
add_library(lyricsplugin MODULE
  lyricsplugin.h
  lyricsplugin.cpp
)
target_link_libraries(lyricsplugin PRIVATE mylyrics)
if(APPLE)
  set(MYPLAYER_PLUGIN_DIR "${CMAKE_BINARY_DIR}/EXEC.app/Contents/PlugIns")
else()
  set(MYPLAYER_PLUGIN_DIR "${CMAKE_BINARY_DIR}/plugins")
endif()
set_target_properties(lyricsplugin PROPERTIES
  LIBRARY_OUTPUT_DIRECTORY "${MYPLAYER_PLUGIN_DIR}"
)
# Also into Tests/plugins, which the test executables load from.
if(BUILD_TESTING)
  add_custom_command(TARGET lyricsplugin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/Tests/plugins"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
      "$<TARGET_FILE:lyricsplugin>" "${CMAKE_BINARY_DIR}/Tests/plugins"
  )
endif()

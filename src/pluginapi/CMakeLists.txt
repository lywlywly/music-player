# The plugin API: header-only, Qt types only; see docs/plugins.md. An
# INTERFACE library, so a plugin linking it gets src/pluginapi/ on its include
# path but not src/, and cannot include app headers.
add_library(pluginapi INTERFACE
  iplayerhost.h
  iplayerlistener.h
  trackinfo.h
)
target_include_directories(pluginapi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pluginapi INTERFACE Qt6::Core)

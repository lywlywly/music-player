# The plugin API: header-only, Qt types only; see
# docs/plugins/plugin-system.md. An INTERFACE library, so a plugin linking it
# gets src/pluginapi/ on its include path but not src/, and cannot include app
# headers.
add_library(pluginapi INTERFACE
  imenuprovider.h
  ipanelprovider.h
  iplayercontrol.h
  iplayerhost.h
  iplayerlistener.h
  iplugin.h
  ipluginhost.h
  isettingspageprovider.h
  isongfieldhost.h
  isongfieldprovider.h
  trackinfo.h
)
target_include_directories(pluginapi INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(pluginapi INTERFACE Qt6::Core)

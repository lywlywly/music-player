# The play stats plugin (docs/plugins/playstats.md). Depends on Qt and the
# plugin API only, not on the app.

# The counting, the play stats file, cloud sync and the settings page. A
# library of its own so the tests link it directly.
add_library(myplaystats STATIC
  cloudplaystatssynccoordinator.h
  cloudplaystatssynccoordinator.cpp
  cloudplaystatssyncservice.h
  cloudplaystatssyncservice.cpp
  cloudsyncsettings.h
  playcompletiontracker.h
  playcompletiontracker.cpp
  playstats.h
  playstats.cpp
  playstatssettingspage.h
  playstatssettingspage.cpp
  playstatsstore.h
  playstatsstore.cpp
)
target_link_libraries(myplaystats PUBLIC
  Qt6::Widgets Qt6::Sql Qt6::Network pluginapi)
# Linked into a shared library.
set_target_properties(myplaystats PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The plugin the app loads: the root object, its fields and its menu.
add_library(playstatsplugin MODULE
  playstatsplugin.h
  playstatsplugin.cpp
  # Embedded by moc (Q_PLUGIN_METADATA); listed for IDEs.
  playstatsplugin.json
)
target_link_libraries(playstatsplugin PRIVATE myplaystats)
myplayer_deploy_plugin(playstatsplugin)

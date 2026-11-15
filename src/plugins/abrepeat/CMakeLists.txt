# The A-B Repeat plugin (docs/plugins/abrepeat.md). Depends on Qt and the
# plugin API only, not on the app.

# The loop, with no UI. A library of its own so the tests link it directly.
add_library(myabrepeat STATIC
  abrepeat.h
  abrepeat.cpp
)
target_link_libraries(myabrepeat PUBLIC Qt6::Core pluginapi)
# Linked into a shared library.
set_target_properties(myabrepeat PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The plugin the app loads: the root object and its menu.
add_library(abrepeatplugin MODULE
  abrepeatplugin.h
  abrepeatplugin.cpp
  # Embedded by moc (Q_PLUGIN_METADATA); listed for IDEs.
  abrepeatplugin.json
)
target_link_libraries(abrepeatplugin PRIVATE myabrepeat Qt6::Widgets)
myplayer_deploy_plugin(abrepeatplugin)

# The spectrum plugin (docs/plugins/spectrum.md). Depends on Qt and the plugin
# API only, not on the app. fft, spectrumbands, spectrumanalyzer and
# spectrumsmoother use no Qt.

# The analysis, the Spectrum panel and its presenter. A library of its own so
# the tests link it directly.
add_library(myspectrum STATIC
  fft.h
  fft.cpp
  spectrumanalyzer.h
  spectrumanalyzer.cpp
  spectrumbands.h
  spectrumbands.cpp
  spectrumpresenter.h
  spectrumpresenter.cpp
  spectrumsmoother.h
  spectrumsmoother.cpp
  spectrumwidget.h
  spectrumwidget.cpp
)
target_link_libraries(myspectrum PUBLIC Qt6::Widgets pluginapi)
# Linked into a shared library.
set_target_properties(myspectrum PROPERTIES POSITION_INDEPENDENT_CODE ON)

# The plugin the app loads: the root object.
add_library(spectrumplugin MODULE
  spectrumplugin.h
  spectrumplugin.cpp
  # Embedded by moc (Q_PLUGIN_METADATA); listed for IDEs.
  spectrumplugin.json
)
target_link_libraries(spectrumplugin PRIVATE myspectrum)
myplayer_deploy_plugin(spectrumplugin)

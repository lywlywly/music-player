# The spectrum module: audio analysis and the Spectrum panel. Depends on Qt
# only, not on the app; see docs/spectrum.md. fft, spectrumbands,
# spectrumanalyzer, spectrumsmoother and pcmtap use no Qt.
add_library(myspectrum STATIC
  fft.h
  fft.cpp
  pcmtap.h
  pcmtap.cpp
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
target_link_libraries(myspectrum PUBLIC Qt6::Widgets)

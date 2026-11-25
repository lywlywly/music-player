target_compile_definitions(MYLib PUBLIC MYPLAYER_TESTING)
# The dummy cloud, also in the plugin tst_mainwindow loads.
target_compile_definitions(myplaystats PUBLIC MYPLAYER_TESTING)

function(SETUP_TESTS)
    foreach(_testname ${ARGN})
        add_executable(${_testname} tst_${_testname}.cpp)
        target_sources(${_testname} PRIVATE test_qsettings_bootstrap.cpp
            test_gstreamer_bootstrap.cpp testaudiofiles.cpp)
        add_test(NAME ${_testname} COMMAND ${_testname})
        target_compile_definitions(${_testname} PRIVATE MYPLAYER_TESTING)
        target_link_libraries(${_testname} Qt6::Test MYLib)
        target_link_directories(${_testname} PRIVATE ${GST_LIBRARY_DIRS})
    endforeach()
endfunction()

set(TEST_NAMES
    basics
    parser
    lyrics
    playbackmanager
    playbackcontroller
    songlibrary
    libraryloader
    columnregistry
    columnlayout
    databasemanager
    dummyaudioplayer
    dummysystemmediainterface
    libraryexpression
    cloudplaystatssyncservice
    cloudplaystatssynccoordinator
    librarysearchdialog_ui
    mainwindow
    playbackqueue
    playcompletiontracker
    nowplayingdisplay
    coverartpresenter
    displayexpression
    settingsdialog_ui
    playlist
    playlisttablemodel
    playlistcollection
    playlisttabs
    playlisttableview
    monitoredlibrary
    autoplaylist
    playlistsearch
    playlistsearchdialog_ui
    efswfilesystemmonitor
    songinfo
    lyricspanel
    abrepeat
    plugins
    spectrum
    spectrumpresenter
    playpositionestimate
    audiotaps
    gstaudioplayer
    replaygain
    playstats
)

SETUP_TESTS(${TEST_NAMES})

# The layout module's tests link the module and Qt only, not MYLib, so the
# module using an app class fails to link them (docs/layout-editing.md). They
# use no settings, files or audio, so they need none of SETUP_TESTS' helpers.
foreach(_testname panellayout panellayoutview panellayouteditor)
  add_executable(${_testname} tst_${_testname}.cpp)
  add_test(NAME ${_testname} COMMAND ${_testname})
  target_link_libraries(${_testname} Qt6::Test mylayout)
endforeach()

# The lyrics code, tested directly (the app loads it as a plugin).
target_link_libraries(lyrics mylyrics)
target_link_libraries(lyricspanel mylyrics)
# The A-B loop, tested directly too.
target_link_libraries(abrepeat myabrepeat)
# The play stats plugin's code, tested directly; tst_playstats also loads the
# plugin from Tests/plugins.
foreach(_test playstats playcompletiontracker cloudplaystatssyncservice
    cloudplaystatssynccoordinator)
  target_link_libraries(${_test} myplaystats)
endforeach()
add_dependencies(playstats playstatsplugin)
# The spectrum code, tested directly.
foreach(_test spectrum spectrumpresenter)
  target_link_libraries(${_test} myspectrum)
endforeach()
# These load the plugins from Tests/plugins.
add_dependencies(mainwindow lyricsplugin abrepeatplugin playstatsplugin
    spectrumplugin)
add_dependencies(plugins lyricsplugin abrepeatplugin playstatsplugin
    spectrumplugin)

set_tests_properties(mainwindow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(coverartpresenter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(settingsdialog_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(librarysearchdialog_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(playlistsearchdialog_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(playlisttabs PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(playlisttableview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(songinfo PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(plugins PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(lyricspanel PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(spectrumpresenter PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(panellayoutview PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(panellayouteditor PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

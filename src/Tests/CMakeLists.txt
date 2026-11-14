target_compile_definitions(MYLib PUBLIC MYPLAYER_TESTING)

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
    plugins
    spectrum
    spectrumpresenter
    audiotaps
    gstaudioplayer
    replaygain
)

SETUP_TESTS(${TEST_NAMES})

# The lyrics code, tested directly (the app loads it as a plugin).
target_link_libraries(lyrics mylyrics)
target_link_libraries(lyricspanel mylyrics)
# These load the lyrics plugin from Tests/plugins.
add_dependencies(mainwindow lyricsplugin)
add_dependencies(plugins lyricsplugin)

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

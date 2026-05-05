target_compile_definitions(MYLib PUBLIC MYPLAYER_TESTING)

function(SETUP_TESTS)
    foreach(_testname ${ARGN})
        add_executable(${_testname} tst_${_testname}.cpp)
        add_test(NAME ${_testname} COMMAND ${_testname})
        target_compile_definitions(${_testname} PRIVATE MYPLAYER_TESTING)
        target_link_libraries(${_testname} Qt6::Test MYLib)
        target_link_directories(${_testname} PRIVATE ${GST_LIBRARY_DIRS})
    endforeach()
endfunction()

set(TEST_NAMES
    basics
    parser
    # lyrics      # temporarily disabled
    # checksum    # temporarily disabled
    playbackmanager
    songlibrary
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
    settingsdialog_ui
    songstore
    playlist
    playlisttabs
)

if(NOT WIN32)
    list(APPEND TEST_NAMES fsmonitor)
endif()

SETUP_TESTS(${TEST_NAMES})

set_tests_properties(mainwindow PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(settingsdialog_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(librarysearchdialog_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

set_tests_properties(playlisttabs PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

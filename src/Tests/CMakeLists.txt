enable_testing()

function(SETUP_TESTS)
    foreach(_testname ${ARGN})
        add_executable(${_testname} tst_${_testname}.cpp)
        add_test(NAME ${_testname} COMMAND ${_testname})
        target_link_libraries(${_testname} Qt${QT_MAJOR_VERISION}::Test MYLib)
        target_link_directories(${_testname} PRIVATE ${GST_LIBRARY_DIRS})
    endforeach()
endfunction()

set(TEST_NAMES
    basics
    parser
    lyrics
    checksum
    playbackmanager
    songlibrary
)

if(NOT WIN32)
    list(APPEND TEST_NAMES fsmonitor)
endif()

SETUP_TESTS(${TEST_NAMES})

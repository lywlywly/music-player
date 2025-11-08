enable_testing()

function(SETUP_TESTS)
    foreach(_testname ${ARGN})
        add_executable(${_testname} tst_${_testname}.cpp)
        add_test(NAME ${_testname} COMMAND ${_testname})
        target_link_libraries(${_testname} Qt${QT_MAJOR_VERISION}::Test MYLib)
        target_link_directories(${_testname} PRIVATE ${GST_LIBRARY_DIRS})
    endforeach()
endfunction()

SETUP_TESTS(
    basics
    parser
    lyrics
    checksum
    fsmonitor
    playbackmanager
    songlibrary
)

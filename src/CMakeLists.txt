cmake_minimum_required(VERSION 3.16)
project(my_player LANGUAGES CXX)

option(BUILD_TESTING "Build tests" OFF)
option(MYPLAYER_USE_BUNDLED_TAGLIB
  "Build TagLib from third_party/taglib submodule" ON)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Multimedia Network Concurrent)
if(LINUX)
  find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()
if(BUILD_TESTING)
  find_package(Qt6 REQUIRED COMPONENTS Test)
endif()

find_package(PkgConfig REQUIRED)
if(APPLE)
  # icu does not install symlinks into /opt/homebrew/ since icu is keg-only
  # adjust as needed
  set(ICU_ROOT "/opt/homebrew/opt/icu4c")
endif()
find_package(ICU REQUIRED COMPONENTS i18n uc data)
if(MYPLAYER_USE_BUNDLED_TAGLIB)
  set(MYPLAYER_BUILD_TESTING_SAVED "${BUILD_TESTING}")
  set(BUILD_TESTING OFF CACHE BOOL "Build tests" FORCE)
  set(BUILD_BINDINGS OFF CACHE BOOL "" FORCE)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/taglib"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/taglib")
  set(BUILD_TESTING "${MYPLAYER_BUILD_TESTING_SAVED}" CACHE BOOL "Build tests" FORCE)
  if(TARGET TagLib::tag)
    set(MYPLAYER_TAGLIB_TARGET TagLib::tag)
  else()
    set(MYPLAYER_TAGLIB_TARGET tag)
  endif()
else()
  find_package(TagLib CONFIG REQUIRED)
  set(MYPLAYER_TAGLIB_TARGET TagLib::tag)
endif()
# efsw (file system watcher) from the third_party/efsw submodule. Built in a
# function so its BUILD_SHARED_LIBS / BUILD_STATIC_LIBS overrides stay local:
# efsw defaults BUILD_SHARED_LIBS to ON, which would also make MYLib shared.
# With both OFF, the `efsw` target is one static library (efsw-static would
# compile the same sources a second time). Requires CMake 3.27 (efsw minimum).
function(myplayer_add_efsw)
  set(BUILD_SHARED_LIBS OFF)
  set(BUILD_STATIC_LIBS OFF)
  set(BUILD_TEST_APP OFF)
  set(BUILD_AGENT OFF)
  set(EFSW_INSTALL OFF)
  add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../third_party/efsw"
    "${CMAKE_CURRENT_BINARY_DIR}/third_party/efsw")
endfunction()
myplayer_add_efsw()

pkg_check_modules(GST IMPORTED_TARGET REQUIRED gstreamer-1.0)

add_subdirectory(spectrum)
add_subdirectory(pluginapi)
add_subdirectory(plugins/lyrics)

if(WIN32)
  find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil)
else()
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat
    libavutil)
endif()

set(PROJECT_SOURCES
  mainwindow.ui
  resources/resources.qrc
  mainwindow.h
  mainwindow.cpp
  addfielddialog.ui
  addfielddialog.h
  addfielddialog.cpp
  audioplayer.h
  audioplayer.cpp
  audiotaps.h
  audiotaps.cpp
  artworkutils.h
  audiofileformats.h
  autoplaylistdialog.h
  autoplaylistdialog.cpp
  cloudplaystatssyncservice.h
  cloudplaystatssyncservice.cpp
  cloudplaystatssynccoordinator.h
  cloudplaystatssynccoordinator.cpp
  columndefinition.h
  columnregistry.h
  columnregistry.cpp
  coverartpresenter.h
  coverartpresenter.cpp
  databasemanager.h
  databasemanager.cpp
  displayexpression.h
  displayexpression.cpp
  displaytheme.h
  displaytheme.cpp
  exprsymbolinfo.h
  fieldformatter.h
  fieldformatter.cpp
  fieldtypepool.h
  fieldtypepool.cpp
  fieldvalue.h
  fieldvalue.cpp
  filemanagerutils.h
  filemanagerutils.cpp
  fieldeditdialog.ui
  fieldeditdialog.h
  fieldeditdialog.cpp
  efswfilesystemmonitor.h
  efswfilesystemmonitor.cpp
  gstaudioplayer.h
  gstaudioplayer.cpp
  globalcolumnlayoutmanager.h
  globalcolumnlayoutmanager.cpp
  ifilesystemmonitor.h
  isystemmediainterface.h
  jumpslider.h
  libraryexpression.h
  libraryexpression_tokenizer.h
  libraryexpression_tokenizer.cpp
  libraryexpression_parser.h
  libraryexpression_parser.cpp
  libraryexpression_ops.h
  libraryexpression_ops.cpp
  libraryloader.h
  libraryloader.cpp
  libraryscanner.h
  libraryscanner.cpp
  libraryfolderspage.h
  libraryfolderspage.cpp
  librarysearchdialog.ui
  librarysearchdialog.h
  librarysearchdialog.cpp
  librarysearchresultsmodel.h
  librarysearchresultsmodel.cpp
  monitoredlibrary.h
  monitoredlibrary.cpp
  nowplayingdisplay.h
  nowplayingdisplay.cpp
  nowplayingpresenter.h
  nowplayingpresenter.cpp
  playbackbackendmanager.h
  playbackbackendmanager.cpp
  playbackbuttonstyle.h
  playbackbuttonstyle.cpp
  playbackcontroller.h
  playbackcontroller.cpp
  playbackmanager.h
  playbackmanager.cpp
  playbackpolicy.h
  playbackpolicy.cpp
  playbackpolicysequential.h
  playbackpolicysequential.cpp
  playbackpolicyshuffle.h
  playbackpolicyshuffle.cpp
  playbackqueue.h
  playbackqueue.cpp
  playcompletiontracker.h
  playcompletiontracker.cpp
  playlist.h
  playlist.cpp
  playlistcollection.h
  playlistcollection.cpp
  playlistdialogs.h
  playlistdialogs.cpp
  playlisttablemodel.h
  playlisttablemodel.cpp
  playlisttableview.h
  playlisttableview.cpp
  playlisttabs.ui
  playlisttabs.h
  playlisttabs.cpp
  playlistsearch.h
  playlistsearch.cpp
  playlistsearchdialog.h
  playlistsearchdialog.cpp
  pluginplayerhost.h
  pluginplayerhost.cpp
  qtaudioplayer.h
  qtaudioplayer.cpp
  settingsdialog.ui
  settingsdialog.h
  settingsdialog.cpp
  songdialogs.h
  songdialogs.cpp
  songlibrary.h
  songlibrary.cpp
  songpropertiesdialog.ui
  songpropertiesdialog.h
  songpropertiesdialog.cpp
  songparser.h
  songparser.cpp
  songinfo.h
  songinfo.cpp
  songinfopanel.h
  songinfopanel.cpp
  songinfopresenter.h
  songinfopresenter.cpp
  statusruntimesymboltable.h
  statusruntimesymboltable.cpp
  systemmediainterfacefactory.h
  systemmediainterfacefactory.cpp
  systemmediapresenter.h
  systemmediapresenter.cpp
  utils.h
)

if(BUILD_TESTING)
  list(APPEND PROJECT_SOURCES
    dummyaudioplayer.h
    dummyaudioplayer.cpp
    dummyfilesystemmonitor.h
    dummysystemmediainterface.h
  )
endif()

if(WIN32)
  list(APPEND PROJECT_SOURCES
    tableselectionstyle.h
    tableselectionstyle.cpp
    windowsmediacenter.h
    windowsmediacenter.cpp
  )
endif()

if(APPLE)
  list(APPEND PROJECT_SOURCES
    macosappearance.h
    macosappearance.mm
    macosmediacenter.h
    macosmediacenter.mm
  )
elseif(LINUX)
  list(APPEND PROJECT_SOURCES
    mprismediainterface.h
    mprismediainterface.cpp
  )
endif()

add_library(MYLib ${PROJECT_SOURCES})
target_include_directories(MYLib PRIVATE ${ICU_INCLUDE_DIRS})

target_link_libraries(
  MYLib
  Qt6::Widgets
  Qt6::Sql
  Qt6::Multimedia
  Qt6::Network
  Qt6::Concurrent
  ${ICU_LIBRARIES}
  ${MYPLAYER_TAGLIB_TARGET}
  PkgConfig::GST
  efsw
  myspectrum
  pluginapi
  mylyrics
)

if(WIN32)
  target_link_libraries(
    MYLib
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    windowsapp
  )
else()
  target_link_libraries(
    MYLib
    PkgConfig::FFMPEG
  )
endif()

if(APPLE)
  set_source_files_properties(macosmediacenter.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc"
  )
  target_link_libraries(
    MYLib
    "-framework CoreAudio"
    "-framework Foundation"
    "-framework MediaPlayer"
  )
elseif(LINUX)
  target_link_libraries(
    MYLib
    Qt6::DBus
  )
endif()

add_executable(EXEC main.cpp)
target_link_libraries(EXEC MYLib)

if(APPLE)
  set_target_properties(EXEC PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "myplayer"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.example.myplayer"
  )
endif()

if(BUILD_TESTING)
  enable_testing()
  add_subdirectory(Tests)
endif()

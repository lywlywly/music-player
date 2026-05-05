cmake_minimum_required(VERSION 3.16)
project(my_player LANGUAGES CXX)

option(BUILD_TESTING "Build tests" OFF)

if(APPLE)
  enable_language(OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Sql Test Multimedia Network Concurrent)
if(LINUX)
  find_package(Qt6 REQUIRED COMPONENTS DBus)
endif()

find_package(PkgConfig REQUIRED)
if(APPLE)
  # icu does not install symlinks into /opt/homebrew/ since icu is keg-only
  # adjust as needed
  set(ICU_ROOT "/opt/homebrew/opt/icu4c")
endif()
find_package(ICU REQUIRED COMPONENTS i18n uc data)
find_package(TagLib CONFIG REQUIRED)
pkg_check_modules(GST IMPORTED_TARGET REQUIRED gstreamer-1.0)

if(WIN32)
  find_package(OpenSSL REQUIRED)
  find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil)
else()
  pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat
    libavutil)
  pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)
endif()

set(PROJECT_SOURCES
  mainwindow.ui
  mainwindow.h
  mainwindow.cpp
  addentrydialog.ui
  addentrydialog.h
  addentrydialog.cpp
  audioplayer.h
  audioplayer.cpp
  artworkutils.h
  checksumcalculator.h
  checksumcalculator.cpp
  cloudplaystatssyncservice.h
  cloudplaystatssyncservice.cpp
  cloudplaystatssynccoordinator.h
  cloudplaystatssynccoordinator.cpp
  columndefinition.h
  columnregistry.h
  columnregistry.cpp
  databasemanager.h
  databasemanager.cpp
  fieldvalue.h
  fieldvalue.cpp
  filesystemcomparer.h
  filesystemcomparer.cpp
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
  librarysearchdialog.ui
  librarysearchdialog.h
  librarysearchdialog.cpp
  librarysearchresultsmodel.h
  librarysearchresultsmodel.cpp
  lyricsloader.h
  lyricsloader.cpp
  lyricsmanager.h
  lyricsmanager.cpp
  lyricspanel.ui
  lyricspanel.h
  lyricspanel.cpp
  playbackbackendmanager.h
  playbackbackendmanager.cpp
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
  playlist.h
  playlist.cpp
  playlisttabs.ui
  playlisttabs.h
  playlisttabs.cpp
  qfilesystemmonitor.h
  qfilesystemmonitor.cpp
  qtaudioplayer.h
  qtaudioplayer.cpp
  settingsdialog.ui
  settingsdialog.h
  settingsdialog.cpp
  songlibrary.h
  songlibrary.cpp
  songpropertiesdialog.ui
  songpropertiesdialog.h
  songpropertiesdialog.cpp
  songparser.h
  songparser.cpp
  songstore.h
  songstore.cpp
  utils.h
)

if(BUILD_TESTING)
  list(APPEND PROJECT_SOURCES
    dummyaudioplayer.h
    dummyaudioplayer.cpp
    dummysystemmediainterface.h
  )
endif()

if(NOT WIN32)
  list(APPEND PROJECT_SOURCES
    efswfilesystemmonitor.h
    efswfilesystemmonitor.cpp
  )
else()
  list(APPEND PROJECT_SOURCES
    windowsmediacenter.h
    windowsmediacenter.cpp
  )
endif()

if(APPLE)
  list(APPEND PROJECT_SOURCES
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
  TagLib::tag
  PkgConfig::GST
)

if(WIN32)
  target_link_libraries(
    MYLib
    OpenSSL::SSL
    OpenSSL::Crypto
    FFmpeg::avcodec
    FFmpeg::avformat
    FFmpeg::avutil
    windowsapp
  )
else()
  target_link_libraries(
    MYLib
    PkgConfig::FFMPEG
    PkgConfig::OPENSSL
  )
endif()

if(APPLE)
  set_source_files_properties(macosmediacenter.mm PROPERTIES
    COMPILE_FLAGS "-fobjc-arc"
  )
  target_link_libraries(
    MYLib
    ${CMAKE_SOURCE_DIR}/lib/libefsw-static-debug.a
    "-framework CoreAudio"
    "-framework Foundation"
    "-framework MediaPlayer"
  )
elseif(LINUX)
  target_link_libraries(
    MYLib
    Qt6::DBus
    ${CMAKE_SOURCE_DIR}/lib/libefsw.so
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

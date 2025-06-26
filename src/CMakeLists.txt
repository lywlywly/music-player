cmake_minimum_required(VERSION 3.16)
project(my_player)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_INCLUDE_CURRENT_DIR ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Test Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql Test
  Multimedia)
find_package(PkgConfig REQUIRED)
if(APPLE)
  set(ICU_ROOT "/opt/homebrew/opt/icu4c")
  set(ICU_INCLUDE_DIR "/opt/homebrew/opt/icu4c/include")
  set(ICU_LIBRARIES
    /opt/homebrew/opt/icu4c@77/lib/libicui18n.dylib
    /opt/homebrew/opt/icu4c@77/lib/libicuuc.dylib
    /opt/homebrew/opt/icu4c@77/lib/libicudata.dylib)
endif()
find_package(ICU REQUIRED COMPONENTS i18n uc data)
find_package(TagLib CONFIG REQUIRED)

pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat
  libavutil)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)
pkg_check_modules(GST REQUIRED gstreamer-1.0)

set(PROJECT_SOURCES
  mainwindow.cpp
  mainwindow.h
  mainwindow.ui
  addentrydialog.h
  addentrydialog.cpp
  addentrydialog.ui
  audioplayer.h
  audioplayer.cpp
  checksumcalculator.h
  checksumcalculator.cpp
  efswfilesystemmonitor.h
  efswfilesystemmonitor.cpp
  filesystemcomparer.h
  filesystemcomparer.cpp
  gstaudioplayer.h
  gstaudioplayer.cpp
  ifilesystemmonitor.h
  lyricsloader.h
  lyricsloader.cpp
  lyricsmanager.h
  lyricsmanager.cpp
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
  qfilesystemmonitor.h
  qfilesystemmonitor.cpp
  qtaudioplayer.h
  qtaudioplayer.cpp
  songlibrary.h
  songlibrary.cpp
  songloader.h
  songloader.cpp
  songparser.h
  songparser.cpp
  songstore.h
  songstore.cpp
  utils.h
)

add_library(MYLib ${PROJECT_SOURCES})

include_directories(MYLib PRIVATE ${ICU_INCLUDE_DIRS})
include_directories(MYLib PRIVATE ${GST_INCLUDE_DIRS})

target_link_libraries(
  MYLib
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Sql
  Qt${QT_VERSION_MAJOR}::Multimedia
  PkgConfig::FFMPEG
  PkgConfig::OPENSSL
  ${ICU_LIBRARIES}
  TagLib::tag
  ${GST_LIBRARIES}
)

if(APPLE)
  target_link_libraries(
    MYLib
    ${CMAKE_SOURCE_DIR}/lib/libefsw-static-debug.a
  )
else()
  target_link_libraries(
    MYLib
    ${CMAKE_SOURCE_DIR}/lib/libefsw.so
  )
endif()

add_executable(EXEC main.cpp)
target_link_libraries(EXEC MYLib)
target_compile_options(EXEC PRIVATE ${GST_CFLAGS_OTHER})

add_subdirectory(Tests)

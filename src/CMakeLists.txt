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
set(ICU_ROOT "/opt/homebrew/opt/icu4c@77")
set(ICU_INCLUDE_DIR "/opt/homebrew/opt/icu4c@77/include")
set(ICU_LIBRARIES
  /opt/homebrew/opt/icu4c@77/lib/libicui18n.dylib
  /opt/homebrew/opt/icu4c@77/lib/libicuuc.dylib
  /opt/homebrew/opt/icu4c@77/lib/libicudata.dylib
)
find_package(ICU REQUIRED COMPONENTS i18n uc data)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat
  libavutil)
pkg_check_modules(OPENSSL REQUIRED IMPORTED_TARGET openssl)

include_directories(include ${ICU_INCLUDE_DIRS})

set(PROJECT_SOURCES
  mainwindow.cpp
  mainwindow.h
  mainwindow.ui
  addentrydialog.h
  addentrydialog.cpp
  addentrydialog.ui
  songparser.h
  songparser.cpp
  lyricsloader.h
  lyricsloader.cpp
  lyricsmanager.h
  lyricsmanager.cpp
  checksumcalculator.h
  checksumcalculator.cpp
  filesystemcomparer.h
  filesystemcomparer.cpp
  ifilesystemmonitor.h
  qfilesystemmonitor.h
  qfilesystemmonitor.cpp
  efswfilesystemmonitor.h
  efswfilesystemmonitor.cpp
  songloader.h
  songloader.cpp
  playbackmanager.h
  playbackmanager.cpp
  playbackqueue.h
  playbackqueue.cpp
  playlist.h
  playlist.cpp
  songstore.h
  songstore.cpp
  songlibrary.h
  songlibrary.cpp
  playbackpolicy.h
  playbackpolicy.cpp
  playbackpolicysequential.h
  playbackpolicysequential.cpp
  playbackpolicyshuffle.h
  playbackpolicyshuffle.cpp
)

add_library(MYLib ${PROJECT_SOURCES})

target_link_libraries(
  MYLib
  Qt${QT_VERSION_MAJOR}::Widgets
  Qt${QT_VERSION_MAJOR}::Sql
  Qt${QT_VERSION_MAJOR}::Multimedia
  PkgConfig::FFMPEG
  PkgConfig::OPENSSL
  ${ICU_LIBRARIES}
  ${CMAKE_SOURCE_DIR}/lib/libefsw-static-debug.a
)

add_executable(EXEC main.cpp)
target_link_libraries(EXEC MYLib)

add_subdirectory(Tests)

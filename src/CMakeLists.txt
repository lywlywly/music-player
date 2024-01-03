cmake_minimum_required(VERSION 3.9)
project(my_player)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Widgets Sql Test Multimedia)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Widgets Sql Test
                                                       Multimedia)
find_package(PkgConfig REQUIRED)
pkg_check_modules(FFMPEG REQUIRED IMPORTED_TARGET libavcodec libavformat
                  libavutil)

set(PROJECT_SOURCES
    mainwindow.cpp
    mainwindow.h
    mainwindow.ui
    songtablemodel.cpp
    songtablemodel.h
    addentrydialog.h
    addentrydialog.cpp
    addentrydialog.ui
    mytableheader.h
    mytableheader.cpp
    playercontrolmodel.h
    playercontrolmodel.cpp
    myproxymodel.h
    myproxymodel.cpp
    songparser.h
    songparser.cpp
    iplaylist.h)

add_library(MYLib ${PROJECT_SOURCES})

target_link_libraries(
  MYLib Qt${QT_VERSION_MAJOR}::Widgets Qt${QT_VERSION_MAJOR}::Sql
  Qt${QT_VERSION_MAJOR}::Multimedia PkgConfig::FFMPEG)

add_executable(EXEC main.cpp)
target_link_libraries(EXEC MYLib)

add_subdirectory(Tests)

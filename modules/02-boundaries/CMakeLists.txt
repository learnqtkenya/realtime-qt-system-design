if(NOT TARGET qtqmlpro_common)
    # Standalone build: cmake -S modules/02-boundaries -B build/m02
    cmake_minimum_required(VERSION 3.25)
    project(m02_boundaries LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick Test)
    qt_standard_project_setup(REQUIRES 6.12)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../common common)
endif()

qt_add_executable(m02_boundaries src/main.cpp)
qt_add_qml_module(m02_boundaries
    URI M02Boundaries
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick
    QML_FILES qml/Main.qml
    SOURCES
        src/FrameStore.h
        src/FrameStore.cpp
        src/PointCloudItem.h
        src/PointCloudItem.cpp
        src/TrackListModel.h
        src/TrackListModel.cpp
        src/EventLogModel.h
        src/EventLogModel.cpp
        src/Scene.h
)
target_include_directories(m02_boundaries PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(m02_boundaries PRIVATE qtqmlpro_common qtqmlpro_commonplugin)

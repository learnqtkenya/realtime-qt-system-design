if(NOT TARGET qtqmlpro_common)
    # Standalone build: cmake -S modules/03-pipeline -B build/m02
    cmake_minimum_required(VERSION 3.25)
    project(m03_pipeline LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick Test)
    qt_standard_project_setup(REQUIRES 6.12)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../common common)
endif()

qt_add_executable(m03_pipeline src/main.cpp)
qt_add_qml_module(m03_pipeline
    URI M03Pipeline
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick
    QML_FILES qml/Main.qml
    SOURCES
        src/FrameStore.h
        src/FrameStore.cpp
        src/PointLayersItem.h
        src/PointLayersItem.cpp
        src/LidarPipeline.h
        src/LidarPipeline.cpp
        src/TrackListModel.h
        src/TrackListModel.cpp
        src/EventLogModel.h
        src/EventLogModel.cpp
        src/Scene.h
)
target_include_directories(m03_pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(m03_pipeline PRIVATE qtqmlpro_common qtqmlpro_commonplugin)

qt_add_executable(tst_pipeline tests/tst_pipeline.cpp src/LidarPipeline.h src/LidarPipeline.cpp)
target_include_directories(tst_pipeline PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_pipeline PRIVATE qtqmlpro_common Qt6::Gui Qt6::Test)
add_test(NAME pipeline COMMAND tst_pipeline)

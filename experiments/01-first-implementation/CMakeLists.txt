if(NOT TARGET qtqmlpro_common)
    # Standalone build: cmake -S experiments/01-first-implementation -B build/m01
    cmake_minimum_required(VERSION 3.25)
    project(m01_first_baseline LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick Test)
    qt_standard_project_setup(REQUIRES 6.12)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../common common)
endif()

qt_add_executable(m01_first src/main.cpp)
qt_add_qml_module(m01_first
    URI M01First
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    QML_FILES qml/Main.qml
    SOURCES
        src/SensorBridge.h
        src/SensorBridge.cpp
        src/EventLogModel.h
        src/EventLogModel.cpp
)
target_include_directories(m01_first PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(m01_first PRIVATE qtqmlpro_common qtqmlpro_commonplugin)

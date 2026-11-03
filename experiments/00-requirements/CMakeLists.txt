if(NOT TARGET qtqmlpro_common)
    # Standalone build: cmake -S experiments/00-requirements -B build/m00
    cmake_minimum_required(VERSION 3.25)
    project(m00_requirements LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick Test)
    qt_standard_project_setup(REQUIRES 6.12)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../common common)
endif()

qt_add_executable(m00_harness_check src/harness_check_main.cpp)
qt_add_qml_module(m00_harness_check
    URI M00Requirements
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    QML_FILES qml/Main.qml
)
target_link_libraries(m00_harness_check PRIVATE qtqmlpro_common qtqmlpro_commonplugin)

qt_add_executable(m00_sim_bench src/sim_bench_main.cpp)
target_link_libraries(m00_sim_bench PRIVATE qtqmlpro_common)

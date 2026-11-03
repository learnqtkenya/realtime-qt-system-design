if(NOT TARGET qtqmlpro_common)
    # Standalone build: cmake -S experiments/04-gpu -B build/m04
    cmake_minimum_required(VERSION 3.25)
    project(m04_gpu LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick Quick3D ShaderTools Test)
    qt_standard_project_setup(REQUIRES 6.12)
    enable_testing()
    add_subdirectory(${CMAKE_CURRENT_LIST_DIR}/../../common common)
endif()

qt_add_executable(m04_gpu src/main.cpp)
qt_add_qml_module(m04_gpu
    URI M04Gpu
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick QtQuick3D
    QML_FILES
        qml/Main.qml
        qml/Quick3DPoints.qml
    SOURCES
        src/LidarMailbox.h
        src/LidarMailbox.cpp
        src/PointViews.h
        src/PointViews.cpp
        src/LidarGeometry.h
        src/LidarGeometry.cpp
        src/FrameStore.h
        src/FrameStore.cpp
        src/TrackListModel.h
        src/TrackListModel.cpp
        src/EventLogModel.h
        src/EventLogModel.cpp
        src/Scene.h
)
# BATCHABLE: the scene graph renderer rewrites material vertex shaders and needs that variant.
qt_add_shaders(m04_gpu "m04_shaders"
    BATCHABLE
    PREFIX "/m04"
    FILES
        shaders/points.vert
        shaders/points.frag
)
target_include_directories(m04_gpu PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# QRhi has limited compatibility guarantees across Qt versions.
target_link_libraries(m04_gpu PRIVATE qtqmlpro_common qtqmlpro_commonplugin Qt6::Quick3D Qt6::GuiPrivate)

qt_add_executable(tst_mailbox tests/tst_mailbox.cpp src/LidarMailbox.h src/LidarMailbox.cpp)
target_include_directories(tst_mailbox PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_mailbox PRIVATE qtqmlpro_common Qt6::Test)
add_test(NAME mailbox COMMAND tst_mailbox)

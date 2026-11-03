set(VIEWER_SOURCES
    src/CanvasFeed.h src/CanvasFeed.cpp
    src/EventLogModel.h src/EventLogModel.cpp
    src/FrameStore.h src/FrameStore.cpp
    src/Lab.h src/Lab.cpp
    src/LidarGeometry.h src/LidarGeometry.cpp
    src/LidarHub.h src/LidarHub.cpp
    src/LidarMailbox.h src/LidarMailbox.cpp
    src/LidarPipeline.h src/LidarPipeline.cpp
    src/PointCloudItem.h src/PointCloudItem.cpp
    src/PointLayersItem.h src/PointLayersItem.cpp
    src/PointViews.h src/PointViews.cpp
    src/Scene.h
    src/Selection.h src/Selection.cpp
    src/TrackIndex.h src/TrackIndex.cpp
    src/InputRobot.h src/InputRobot.cpp
    src/TrackListModel.h src/TrackListModel.cpp
)

qt_add_executable(viewer src/main.cpp)
set_source_files_properties(qml/ViewSettings.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(viewer
    URI Viewer
    VERSION 1.0
    IMPORT_PATH ${CMAKE_BINARY_DIR}/qml
    DEPENDENCIES QtQuick QtQuick3D QtQuick3D.Helpers QtQuick.Controls
    QML_FILES
        qml/Main.qml
        qml/Viewport.qml
        qml/LabPanel.qml
        qml/MetricChart.qml
        qml/ViewSettings.qml
        qml/Quick3DPoints.qml
        qml/Orbit3DView.qml
    RESOURCES
        qml/heightcolor.vert
        qml/heightcolor.frag
    SOURCES ${VIEWER_SOURCES}
)
# BATCHABLE: the scene graph renderer rewrites material vertex shaders and needs that variant.
qt_add_shaders(viewer "viewer_shaders"
    BATCHABLE
    PREFIX "/viewer"
    FILES
        shaders/points.vert
        shaders/points.frag
)
target_include_directories(viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
# QRhi has limited compatibility guarantees across Qt versions.
target_link_libraries(viewer PRIVATE
    qtqmlpro_common qtqmlpro_commonplugin
    Qt6::Quick3D Qt6::QuickControls2 Qt6::GuiPrivate)

qt_add_executable(tst_viewer tests/tst_viewer.cpp
    src/Lab.h src/Lab.cpp
    src/LidarHub.h src/LidarHub.cpp
    src/LidarMailbox.h src/LidarMailbox.cpp
    src/LidarPipeline.h src/LidarPipeline.cpp)
target_include_directories(tst_viewer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_viewer PRIVATE qtqmlpro_common Qt6::Gui Qt6::Test)
add_test(NAME viewer COMMAND tst_viewer)

qt_add_executable(tst_trackindex tests/tst_trackindex.cpp src/TrackIndex.h src/TrackIndex.cpp)
target_include_directories(tst_trackindex PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_trackindex PRIVATE qtqmlpro_common Qt6::Test)
add_test(NAME trackindex COMMAND tst_trackindex)

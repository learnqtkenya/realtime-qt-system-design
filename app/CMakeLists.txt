find_package(PkgConfig REQUIRED)
pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
find_package(ZLIB REQUIRED)
find_package(Qt6 REQUIRED COMPONENTS Network)

set(VIEWER_SOURCES
    src/Codec.h src/Codec.cpp
    src/FileSource.h src/FileSource.cpp
    src/History.h src/History.cpp
    src/Recording.h src/Recording.cpp
    src/Source.h
    src/Presenter.h src/Presenter.cpp
    src/Proxies.h src/Proxies.cpp
    src/TrackInstancing.h src/TrackInstancing.cpp
    src/TrackOverlay.h src/TrackOverlay.cpp
    src/HealthMonitor.h src/HealthMonitor.cpp
    src/StreamServer.h src/StreamServer.cpp
    src/Wire.h src/Wire.cpp
    src/Overlay.h src/Overlay.cpp
    src/Plugins.h src/Plugins.cpp
    src/ScriptLayer.h src/ScriptLayer.cpp
    src/ProcessLayer.h src/ProcessLayer.cpp
    src/LayerOverlay.h src/LayerOverlay.cpp
    src/Validate.h
    src/Timeline.h src/Timeline.cpp
    src/CameraFeed.h src/CameraFeed.cpp
    src/Cameras.h src/Cameras.cpp
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
    DEPENDENCIES QtQuick QtQuick3D QtQuick3D.Helpers QtQuick.Controls QtMultimedia
    QML_FILES
        qml/Main.qml
        qml/Viewport.qml
        qml/LabPanel.qml
        qml/MetricChart.qml
        qml/ViewSettings.qml
        qml/Quick3DPoints.qml
        qml/Orbit3DView.qml
        qml/CameraStrip.qml
        qml/TimelineBar.qml
        qml/DataPanel.qml
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
    Qt6::Quick3D Qt6::QuickControls2 Qt6::Multimedia Qt6::GuiPrivate Qt6::Network PkgConfig::ZSTD ZLIB::ZLIB)

# Plugins live next to the viewer in the build (and in a package): build/<preset>/plugins.
add_custom_target(viewer_plugins ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins-lab ${CMAKE_BINARY_DIR}/plugins-lab
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins/sdk ${CMAKE_BINARY_DIR}/plugins-lab/sdk
    COMMENT "Copying plugins")
add_dependencies(viewer viewer_plugins)
# A process plugin in C++ with no Qt: the protocol is the API.
add_executable(speed-vectors-layer ${CMAKE_SOURCE_DIR}/plugins/speed-vectors-cpp/main.cpp)
target_include_directories(speed-vectors-layer PRIVATE ${CMAKE_SOURCE_DIR}/plugins/sdk/cpp)
set_target_properties(speed-vectors-layer PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/plugins/speed-vectors-cpp)
add_dependencies(speed-vectors-layer viewer_plugins)
add_dependencies(viewer speed-vectors-layer)

qt_add_executable(tst_viewer tests/tst_viewer.cpp
    src/Lab.h src/Lab.cpp
    src/HealthMonitor.h src/HealthMonitor.cpp
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

qt_add_executable(bench_codec tests/bench_codec.cpp src/Codec.h src/Codec.cpp)
target_include_directories(bench_codec PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(bench_codec PRIVATE qtqmlpro_common PkgConfig::ZSTD)

qt_add_executable(tst_recording tests/tst_recording.cpp
    src/Codec.h src/Codec.cpp src/Recording.h src/Recording.cpp
    src/History.h src/History.cpp src/FileSource.h src/FileSource.cpp src/Source.h)
target_include_directories(tst_recording PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_recording PRIVATE qtqmlpro_common Qt6::Test PkgConfig::ZSTD ZLIB::ZLIB)
add_test(NAME recording COMMAND tst_recording)

qt_add_executable(tst_presenter tests/tst_presenter.cpp
    src/Codec.h src/Codec.cpp src/Recording.h src/Recording.cpp src/Presenter.h src/Presenter.cpp
    src/History.h src/History.cpp src/FileSource.h src/FileSource.cpp src/Source.h)
target_include_directories(tst_presenter PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_presenter PRIVATE qtqmlpro_common Qt6::Test PkgConfig::ZSTD ZLIB::ZLIB)
add_test(NAME presenter COMMAND tst_presenter)

qt_add_executable(tst_models tests/tst_models.cpp
    src/TrackListModel.h src/TrackListModel.cpp src/EventLogModel.h src/EventLogModel.cpp
    src/FrameStore.h src/FrameStore.cpp src/Proxies.h src/Proxies.cpp)
target_include_directories(tst_models PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_models PRIVATE qtqmlpro_common Qt6::Test Qt6::Qml)
add_test(NAME models COMMAND tst_models)

qt_add_executable(tst_wire tests/tst_wire.cpp
    src/StreamServer.h src/StreamServer.cpp src/Wire.h src/Wire.cpp src/Overlay.h src/Overlay.cpp
    src/Codec.h src/Codec.cpp src/Recording.h src/Recording.cpp)
target_include_directories(tst_wire PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_wire PRIVATE qtqmlpro_common Qt6::Test Qt6::Network Qt6::Qml PkgConfig::ZSTD ZLIB::ZLIB)
add_test(NAME wire COMMAND tst_wire)

qt_add_executable(tst_plugins tests/tst_plugins.cpp
    src/Plugins.h src/Plugins.cpp src/ScriptLayer.h src/ScriptLayer.cpp src/ProcessLayer.h src/ProcessLayer.cpp
    src/Overlay.h src/Overlay.cpp src/Wire.h src/Wire.cpp src/Codec.h src/Codec.cpp src/Recording.h src/Recording.cpp
    src/FrameStore.h src/FrameStore.cpp src/Selection.h src/Selection.cpp src/TrackIndex.h src/TrackIndex.cpp)
target_include_directories(tst_plugins PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_compile_definitions(tst_plugins PRIVATE PLUGINS_DIR="${CMAKE_BINARY_DIR}/plugins" PLUGINS_LAB_DIR="${CMAKE_BINARY_DIR}/plugins-lab")
target_link_libraries(tst_plugins PRIVATE qtqmlpro_common Qt6::Test Qt6::Quick Qt6::Network PkgConfig::ZSTD ZLIB::ZLIB)
add_dependencies(tst_plugins viewer_plugins speed-vectors-layer)
add_test(NAME plugins COMMAND tst_plugins)

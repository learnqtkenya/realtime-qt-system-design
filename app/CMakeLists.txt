if(EMSCRIPTEN)
    # The WASM kit has neither; zstd is built from the release the desktop uses, zlib is Emscripten's port.
    include(FetchContent)
    set(ZSTD_BUILD_PROGRAMS OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(ZSTD_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(ZSTD_MULTITHREAD_SUPPORT OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(zstd
        URL https://github.com/facebook/zstd/releases/download/v1.5.7/zstd-1.5.7.tar.gz
        URL_HASH SHA256=eb33e51f49a15e023950cd7825ca74a4a2b43db8354825ac24fc1b7ee09e6fa3
        SOURCE_SUBDIR build/cmake
        EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(zstd)
    add_library(qp_zstd INTERFACE)
    target_include_directories(qp_zstd INTERFACE ${zstd_SOURCE_DIR}/lib)
    target_link_libraries(qp_zstd INTERFACE libzstd_static)
    add_library(qp_zlib INTERFACE)
    target_compile_options(qp_zlib INTERFACE -sUSE_ZLIB=1)
    target_link_options(qp_zlib INTERFACE -sUSE_ZLIB=1)
    set(QP_ZSTD qp_zstd)
    set(QP_ZLIB qp_zlib)
else()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(ZSTD REQUIRED IMPORTED_TARGET libzstd)
    find_package(ZLIB REQUIRED)
    set(QP_ZSTD PkgConfig::ZSTD)
    set(QP_ZLIB ZLIB::ZLIB)
endif()
find_package(Qt6 REQUIRED COMPONENTS Network WebSockets)

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
    src/LayerOverlay.h src/LayerOverlay.cpp
    src/Diagnostics.h src/Diagnostics.cpp
    src/LayoutStore.h src/LayoutStore.cpp
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

if(NOT EMSCRIPTEN) # a browser can't start processes
    list(APPEND VIEWER_SOURCES src/ProcessLayer.h src/ProcessLayer.cpp)
endif()

qt_add_executable(viewer src/main.cpp)
add_custom_target(viewer_version
    COMMAND ${CMAKE_COMMAND} -DSRC=${CMAKE_SOURCE_DIR} -DOUT=${CMAKE_CURRENT_BINARY_DIR}/generated/qp_version.h
            -P ${CMAKE_SOURCE_DIR}/cmake/version.cmake
    BYPRODUCTS ${CMAKE_CURRENT_BINARY_DIR}/generated/qp_version.h)
add_dependencies(viewer viewer_version)
target_include_directories(viewer PRIVATE ${CMAKE_CURRENT_BINARY_DIR}/generated)
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
    Qt6::Quick3D Qt6::QuickControls2 Qt6::Multimedia Qt6::GuiPrivate Qt6::Network Qt6::WebSockets ${QP_ZSTD} ${QP_ZLIB})

# Plugins live next to the viewer in the build (and in a package): build/<preset>/plugins.
add_custom_target(viewer_plugins ALL
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins ${CMAKE_BINARY_DIR}/plugins
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins-lab ${CMAKE_BINARY_DIR}/plugins-lab
    COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_SOURCE_DIR}/plugins/sdk ${CMAKE_BINARY_DIR}/plugins-lab/sdk
    COMMENT "Copying plugins")
add_dependencies(viewer viewer_plugins)
if(EMSCRIPTEN)
    # Threads are Web Workers made ahead of time; more threads than this start only once the main
    # thread yields to the browser. Qt's default is 4; the viewer starts 10 (results/step-11).
    set(QP_WASM_POOL 4 CACHE STRING "Web Workers created before main() runs")
    set_target_properties(viewer PROPERTIES QT_WASM_PTHREAD_POOL_SIZE ${QP_WASM_POOL})
    option(QP_WASM_ASSERTIONS "Emscripten runtime assertions (diagnosis only; never measure with this)" OFF)
    if(QP_WASM_ASSERTIONS)
        target_link_options(viewer PRIVATE -sASSERTIONS=2 --profiling-funcs)
    endif()
    # The page, and the plugins' manifests and scripts copied into the page's in-memory filesystem
    # at /plugins (the viewer's directory is / in a browser). Served from build/wasm.
    configure_file(web/index.html ${CMAKE_CURRENT_BINARY_DIR}/index.html COPYONLY)
    file(GLOB_RECURSE plugin_files RELATIVE ${CMAKE_SOURCE_DIR}/plugins CONFIGURE_DEPENDS
         ${CMAKE_SOURCE_DIR}/plugins/*/plugin.json ${CMAKE_SOURCE_DIR}/plugins/*/*.mjs ${CMAKE_SOURCE_DIR}/plugins/*/*.qml)
    set(preload "")
    foreach(f IN LISTS plugin_files)
        string(APPEND preload "  {\"source\": \"../plugins/${f}\", \"destination\": \"/plugins/${f}\"},\n")
    endforeach()
    string(REGEX REPLACE ",\n$" "\n" preload "${preload}")
    file(WRITE ${CMAKE_CURRENT_BINARY_DIR}/preload.json "[\n${preload}]\n")
endif()

if(NOT EMSCRIPTEN)
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
target_link_libraries(tst_wire PRIVATE qtqmlpro_common Qt6::Test Qt6::Network Qt6::WebSockets Qt6::Qml PkgConfig::ZSTD ZLIB::ZLIB)
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

qt_add_executable(tst_ops tests/tst_ops.cpp src/Diagnostics.h src/Diagnostics.cpp src/LayoutStore.h src/LayoutStore.cpp)
target_include_directories(tst_ops PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(tst_ops PRIVATE qtqmlpro_common Qt6::Test Qt6::Qml Qt6::Network)
add_test(NAME ops COMMAND tst_ops)

# Step 9: a package. `cmake --install build/release --prefix <dir>` gives a directory that runs
# without /opt/Qt: bin/viewer, the Qt libraries, plugins and QML modules it uses (Qt's deploy
# script), and the viewer's own read-only data in share/qtqmlpro.
include(GNUInstallDirs)
install(TARGETS viewer RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
# Qt's default deploys only the X11 platform plugin; on a Wayland desktop that means XWayland.
qt_generate_deploy_script(TARGET viewer OUTPUT_SCRIPT viewer_deploy CONTENT "
qt_deploy_qml_imports(TARGET viewer PLUGINS_FOUND plugins_found)
qt_deploy_runtime_dependencies(
    EXECUTABLE \"\${QT_DEPLOY_BIN_DIR}/$<TARGET_FILE_NAME:viewer>\"
    ADDITIONAL_MODULES \${plugins_found}
    INCLUDE_PLUGINS qwayland qxcb
    GENERATE_QT_CONF)
")
install(SCRIPT ${viewer_deploy})
install(DIRECTORY ${CMAKE_BINARY_DIR}/plugins/ DESTINATION ${CMAKE_INSTALL_DATADIR}/qtqmlpro/plugins USE_SOURCE_PERMISSIONS)
install(DIRECTORY ${CMAKE_BINARY_DIR}/plugins-lab/ DESTINATION ${CMAKE_INSTALL_DATADIR}/qtqmlpro/plugins-lab
        USE_SOURCE_PERMISSIONS)
install(FILES ${CMAKE_BINARY_DIR}/media/cam0.mp4 ${CMAKE_BINARY_DIR}/media/cam1.mp4 ${CMAKE_BINARY_DIR}/media/cam2.mp4
              ${CMAKE_BINARY_DIR}/media/cam3.mp4
        DESTINATION ${CMAKE_INSTALL_DATADIR}/qtqmlpro/media OPTIONAL)
endif()

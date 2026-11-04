qt_add_library(qtqmlpro_common STATIC)

# Keep QML files at the module root so they see the module's own C++ types.
set_source_files_properties(qml/Hud.qml PROPERTIES QT_RESOURCE_ALIAS Hud.qml)

qt_add_qml_module(qtqmlpro_common
    URI QtQMLPro.Common
    VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/qml/QtQMLPro/Common
    DEPENDENCIES QtQuick
    SOURCES
        include/qp/SimTypes.h
        include/qp/RunningSamples.h
        include/qp/StageStats.h
        include/qp/AppOptions.h
        include/qp/SensorSimulator.h
        include/qp/FrameMonitor.h
        include/qp/Sparkline.h
        src/StageStats.cpp
        src/AppOptions.cpp
        src/SensorSimulator.cpp
        src/FrameMonitor.cpp
        src/Sparkline.cpp
    QML_FILES
        qml/Hud.qml
)

# include/qp is also on the path because the generated QML type registration
# code includes headers by bare file name.
target_include_directories(qtqmlpro_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/include/qp)
target_link_libraries(qtqmlpro_common PUBLIC Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick)
# QRhi (GPU timing) has limited compatibility guarantees across Qt versions.
target_link_libraries(qtqmlpro_common PRIVATE Qt6::GuiPrivate)
target_compile_options(qtqmlpro_common PUBLIC -Wall -Wextra)

# ---- tests -----------------------------------------------------------------
if(NOT EMSCRIPTEN)
find_package(Qt6 REQUIRED COMPONENTS Test)

qt_add_executable(tst_runningsamples tests/tst_runningsamples.cpp)
target_link_libraries(tst_runningsamples PRIVATE qtqmlpro_common Qt6::Test)
add_test(NAME runningsamples COMMAND tst_runningsamples)

qt_add_executable(tst_simulator tests/tst_simulator.cpp)
target_link_libraries(tst_simulator PRIVATE qtqmlpro_common Qt6::Test)
add_test(NAME simulator COMMAND tst_simulator)
endif()

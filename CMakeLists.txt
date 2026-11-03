cmake_minimum_required(VERSION 3.25)
project(QtQMLPro VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick QuickControls2 Quick3D ShaderTools Multimedia Test)
qt_standard_project_setup(REQUIRES 6.12)

enable_testing()

add_subdirectory(common)
add_subdirectory(experiments/00-requirements)
add_subdirectory(experiments/01-first-implementation)
add_subdirectory(experiments/02-boundaries)
add_subdirectory(experiments/03-pipeline)
add_subdirectory(experiments/04-gpu)
add_subdirectory(experiments/05-video-seek)
add_subdirectory(app)

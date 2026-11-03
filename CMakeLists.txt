cmake_minimum_required(VERSION 3.25)
project(QtQMLPro VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Qt6 6.12 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick Quick3D ShaderTools Test)
qt_standard_project_setup(REQUIRES 6.12)

enable_testing()

add_subdirectory(common)
add_subdirectory(modules/00-requirements)
add_subdirectory(modules/01-first-implementation)
add_subdirectory(modules/02-boundaries)
add_subdirectory(modules/03-pipeline)
add_subdirectory(modules/04-gpu)

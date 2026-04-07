cmake_minimum_required(VERSION 3.21)
project(rocky_qt_docking VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)

# Find Qt Advanced Docking System
find_package(qtadvanceddocking-qt6 REQUIRED)

# Find Rocky and its dependencies
find_package(rocky CONFIG REQUIRED)
find_package(vsg CONFIG REQUIRED)
find_package(glslang CONFIG REQUIRED)
find_package(vsgQt CONFIG REQUIRED)

add_executable(${PROJECT_NAME} main.cpp)

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ads::qtadvanceddocking-qt6
    rocky::rocky
    vsgQt::vsgQt
)

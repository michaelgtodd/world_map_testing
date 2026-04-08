cmake_minimum_required(VERSION 3.21)
project(rocky_qt_docking VERSION 0.1.0 LANGUAGES CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Set RPATH so the binary finds shared libs without LD_LIBRARY_PATH
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH ON)
set(CMAKE_BUILD_WITH_INSTALL_RPATH ON)

# ── Dependencies ─────────────────────────────────────────────────────

find_package(Qt6 REQUIRED COMPONENTS Core Gui Widgets)
find_package(qtadvanceddocking-qt6 REQUIRED)
find_package(rocky CONFIG REQUIRED)
find_package(vsgQt CONFIG REQUIRED)

# ── Main executable ──────────────────────────────────────────────────

add_executable(${PROJECT_NAME}
    src/main.cpp
    src/FlightPlan.cpp
    src/widgets/EarthViewPane.cpp
    # Headers with Q_OBJECT listed for AUTOMOC (not included by any .cpp)
    src/widgets/FlightSettingsPanel.h
    src/widgets/FlightInfoOverlay.h
    src/widgets/NavWidget.h
)

target_include_directories(${PROJECT_NAME} PRIVATE ${PROJECT_SOURCE_DIR}/src)
target_compile_definitions(${PROJECT_NAME} PRIVATE PROJECT_SOURCE_DIR="${PROJECT_SOURCE_DIR}")

target_link_libraries(${PROJECT_NAME} PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    ads::qtadvanceddocking-qt6
    rocky::rocky
    vsgQt::vsgQt
)

# ── Tests ────────────────────────────────────────────────────────────

option(BUILD_TESTS "Build unit tests" ON)

if(BUILD_TESTS)
    enable_testing()
    find_package(GTest REQUIRED)
    find_package(glm CONFIG REQUIRED)

    add_executable(tests
        tests/test_geodesy.cpp
        tests/test_waypoint.cpp
    )

    target_include_directories(tests PRIVATE ${PROJECT_SOURCE_DIR}/src)
    target_link_libraries(tests PRIVATE GTest::gtest_main glm::glm)

    include(GoogleTest)
    gtest_discover_tests(tests)
endif()

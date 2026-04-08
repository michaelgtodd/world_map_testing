# Rocky Flight Planner

A C++ flight planning application built on [Rocky](https://github.com/pelicanmapping/rocky) (Vulkan earth renderer), Qt6, and the [Qt Advanced Docking System](https://github.com/githubuser0xFFFF/Qt-Advanced-Docking-System). Place waypoints on a 3D globe, configure altitude and speed, and generate textual flight plans with great-circle route visualization.

## Features

- **3D Earth View** -- Rocky/Vulkan globe with TMS imagery and elevation, embedded in a Qt docking pane
- **Flight Planning** -- Right-click to place FlyTo waypoints with configurable altitude (MSL/AGL) and speed (knots)
- **Approach to Hover** -- Click-and-drag to define an approach with inbound bearing, automatic 3-degree glideslope from cruise altitude to 20 ft AGL
- **Great-Circle Routes** -- Route lines follow earth curvature via spherical interpolation
- **Flight Plan Display** -- Formatted text output with leg distances, bearings, and estimated time enroute
- **Floating HUD Overlays** -- Flight settings panel, info overlay, and navigation widget float over the earth view
- **Dockable Panels** -- Waypoint table and flight plan text in rearrangeable dock panes
- **Camera Controls** -- Ctrl+Left-drag to rotate/tilt, navigation widget with zoom/rotate/home buttons

## Dependencies

- **Qt6** (Core, Gui, Widgets)
- **Qt Advanced Docking System** (built from source)
- **Rocky** 1.0.2 (built from source with vcpkg)
- **vsgQt** (built from source)
- **VulkanSceneGraph** (via vcpkg, pulled by Rocky)
- **Google Test** (for unit tests)

System packages (Ubuntu 24.04):

```bash
sudo apt-get install -y \
    cmake build-essential ninja-build git curl zip unzip tar pkg-config \
    qt6-base-dev qt6-base-private-dev qt6-tools-dev libqt6svg6-dev qt6-declarative-dev \
    libvulkan-dev vulkan-tools vulkan-validationlayers \
    libxcb1-dev libxcb-keysyms1-dev libxcb-image0-dev libxcb-shm0-dev \
    libxcb-icccm4-dev libxcb-sync-dev libxcb-xfixes0-dev libxcb-shape0-dev \
    libxcb-randr0-dev libxcb-render-util0-dev libxcb-util-dev libxcb-xkb-dev \
    libxcb-xinerama0-dev libxcb-cursor-dev libxkbcommon-dev \
    libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev \
    libgl1-mesa-dev libglu1-mesa-dev \
    autoconf autoconf-archive automake libtool bison flex \
    python3 ninja-build libgtest-dev
```

## Building

### Quick Start (with prebuilt dependencies)

If Rocky, vsgQt, and Qt Advanced Docking System are already installed to `/opt/local` (or similar):

```bash
cmake --preset default
cmake --build --preset default
```

The CMake preset in `CMakePresets.json` contains the prefix paths. Edit it to match your install locations.

### Building All Dependencies from Source

See `docker/Dockerfile` for the complete reproducible build from a clean Ubuntu 24.04. The key steps are:

1. **vcpkg** -- Clone and bootstrap
2. **Qt Advanced Docking System** -- Build with Qt6 support
3. **Rocky** -- Build with vcpkg toolchain (Qt demo disabled, ImGui disabled with patches from `docker/rocky-imgui-fix.patch`)
4. **vsgQt** -- Build against system Qt6 and vcpkg's VSG

### Docker (Clean Build Test)

Builds everything from scratch on Ubuntu 24.04 and runs tests:

```bash
docker/build.sh
```

## Running

```bash
./run.sh
```

The run script sets `ROCKY_FILE_PATH` (for shaders/resources) and `QT_QPA_PLATFORM=xcb` (required because vsgQt creates XCB Vulkan surfaces).

## Testing

```bash
./test.sh                    # run all tests
./test.sh -R Geodesy         # run only geodesy tests
ctest --preset default       # via CMake preset
```

## Usage

| Action | Effect |
|--------|--------|
| Left-drag | Pan the globe |
| Ctrl+Left-drag | Rotate/tilt the camera |
| Scroll wheel | Zoom |
| Right-click | Add waypoint (FlyTo mode) |
| Right-click+drag | Add approach waypoint with inbound bearing (Approach mode) |

Switch between **FLY TO** and **APPROACH TO HOVER** modes in the floating settings panel (top-right of the earth view).

## Project Structure

```
main.cpp                        Application entry point and wiring
Geodesy.h                       Geodesic math (haversine, bearing, great-circle)
Waypoint.h                      Data types (Waypoint, WaypointQueue, FlightSettings)
FlightPlan.h/.cpp               Flight plan state, route lines, text formatting
WaypointHandler.h               VSG right-click event handler
RockyViewer.h                   Qt-VSG viewer bridge
widgets/
  EarthViewPane.h/.cpp          Earth view container + floating overlay management
  FlightSettingsPanel.h         Settings overlay (altitude, speed, mode)
  FlightInfoOverlay.h           Last-waypoint info display
  NavWidget.h                   Zoom/rotate/home navigation buttons
tests/
  test_geodesy.cpp              Geodesy function tests (21 tests)
  test_waypoint.cpp             FlightSettings tests
docker/
  Dockerfile                    Clean Ubuntu 24.04 build test
  build.sh                      Docker build script
  rocky-imgui-fix.patch         Patches for building Rocky without ImGui
```

## License

MIT

# Requirements and Features

## Core Requirements

### Earth Rendering
- [x] Display a Rocky (Vulkan) earth scene in a Qt6 application
- [x] ArcGIS World Imagery satellite tiles
- [x] MapZen Terrarium elevation data (zoom 0-15)
- [x] Earth view embedded in a Qt Advanced Docking System pane
- [x] Externalized map configuration (`data/default.map.json`)

### Qt Widget Overlays on Earth View
- [x] Render Qt widgets on top of the Vulkan earth view
- [x] Floating settings panel (top-right) with interactive controls
- [x] Info overlay (top-left) showing last waypoint info
- [x] Navigation widget (top-left, below info) with zoom/rotate/home buttons
- [x] Overlays track the earth pane position when window is moved/resized/undocked

### Flight Planning -- FlyTo Waypoints
- [x] Right-click on the earth to place waypoints
- [x] Configurable altitude in feet (0--60,000 ft range, default 50 ft)
- [x] Altitude reference: MSL or AGL (default AGL)
- [x] Configurable speed in knots (10--2,000 kts range)
- [x] Waypoint markers rendered on the globe (blue dots)
- [x] Route lines connecting waypoints

### Flight Planning -- Approach to Hover
- [x] Separate mode selectable in the settings panel
- [x] Click-and-drag to specify hover point and inbound bearing
- [x] Drag direction defines the inbound approach bearing
- [x] Automatic 3-degree glideslope computation from previous waypoint altitude
- [x] Hover altitude fixed at 20 ft AGL
- [x] FAF (Final Approach Fix) auto-placed at computed distance along reciprocal bearing
- [x] FAF marker (yellow) and hover point marker (green) rendered on globe
- [x] Glideslope leg rendered as part of route line

### Route Visualization
- [x] Great-circle route lines that follow earth curvature (spherical interpolation)
- [x] Maximum 50 nm per interpolated segment for smooth arcs
- [x] Approach legs use 5 nm segments for smooth glideslope visualization
- [x] Line geometry updated in-place via recycle()+dirty() pattern

### Flight Plan Text Display
- [x] Formatted textual flight plan in a dockable pane
- [x] Per-waypoint: coordinates, altitude, speed, type
- [x] Per-leg: distance (nm), bearing (degrees true), estimated time enroute
- [x] Approach waypoints show: inbound bearing, FAF altitude, glideslope angle, approach leg distance
- [x] Totals: distance, ETE, waypoint count

### Camera Controls
- [x] Left-drag to pan
- [x] Ctrl+Left-drag to rotate/tilt camera (via QApplication event filter)
- [x] Scroll wheel to zoom
- [x] Navigation widget buttons for zoom in/out, rotate left/right, tilt up/down, home
- [x] Nav widget zoom uses screen center (not mouse position)
- [x] Initial camera at Sikorsky Stratford (41.25303N, 73.09147W, 2500ft)

### Docking System
- [x] Earth view as center dock (dominant area)
- [x] Waypoint table as right dock (narrow, 280--420px)
- [x] Flight plan text as bottom dock
- [x] All docks toggleable from View menu
- [x] Clear flight plan from File menu and settings panel button

### Build and Test
- [x] CMake build system with presets
- [x] RPATH baked into binary (no LD_LIBRARY_PATH needed)
- [x] Google Test unit tests (21 tests: 18 geodesy, 3 flight settings)
- [x] Test runner script (`scripts/test.sh`)
- [x] Docker-based clean build test (Ubuntu 24.04 from scratch)
- [x] Run script (`scripts/run.sh`) with required environment variables
- [x] Pinned dependency versions (vcpkg 2026.03.18, ADS 4.5.0, Rocky v1.0.0, vsgQt v0.4.0)

## Non-Functional Requirements

- Application must handle the Vulkan/Qt platform mismatch (vsgQt uses XCB surfaces, system may default to Wayland)
- Rocky built with ImGui enabled (the default) to avoid source patches
- Floating overlay widgets must not block mouse events from reaching the Vulkan earth view
- Flight settings changes must be thread-safe (atomic variables shared between Qt and VSG threads)
- depthOffset set to 0 for accurate rendering at low altitudes (large values displace geometry toward camera)
- When terrain tiles haven't loaded, altitude clamped to minimum 1m above WGS84 ellipsoid

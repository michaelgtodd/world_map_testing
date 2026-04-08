# Architecture Notes

## Overview

The application has three main layers:

1. **Rocky/VSG** -- Vulkan-based earth rendering running its own frame loop
2. **Qt6** -- Widget toolkit managing the window, docking, and UI overlays
3. **Bridge** -- vsgQt provides the `vsgQt::Window` (a `QWindow` subclass) that hosts the Vulkan surface inside a Qt widget hierarchy

## Map Configuration

Map layers and terrain settings are loaded from `data/default.map.json` at startup via `app.mapNode->from_json()`. This externalizes imagery sources, elevation sources, and terrain quality settings so they can be changed without recompiling.

Current data sources:
- **Imagery**: ArcGIS World Imagery (`services.arcgisonline.com`), accessed as XYZ tiles with `profile: spherical-mercator` to bypass TMS manifest discovery
- **Elevation**: MapZen Terrarium via AWS S3, PNG-encoded terrain height (zoom 0-15)

Sky and atmosphere settings (sun position, ambient light) remain in code since Rocky's JSON config doesn't support them.

## Threading Model

Rocky's rendering runs inside vsgQt's `Viewer::render()` method, which is called by a Qt timer. This is effectively single-threaded from Qt's perspective, but the VSG event handlers run during the frame call, not during Qt event processing.

Communication between the VSG event handler (which processes right-clicks) and Qt widgets (which display waypoint data) is done via a **thread-safe queue** (`WaypointQueue`). A 50ms Qt timer polls the queue and updates the UI.

Shared settings (altitude, speed, mode) use `std::atomic` variables in `FlightSettings`, written by Qt widget callbacks and read by the VSG event handler.

## Key Architectural Decisions

### Native Window Container Limitations

`QWidget::createWindowContainer()` wraps the vsgQt `QWindow` into a widget, but this creates a **native/foreign X11 window** that has significant limitations:

- **Child widgets don't render on top of it.** Qt paints child widgets into the parent's backing store, but the native window has its own surface. Overlaying QWidgets as children of the window container results in a black pane.

- **Widget-level event filters don't receive events.** Mouse and keyboard events go directly to the native X11 window, bypassing Qt's widget event dispatch. `QWidget::installEventFilter()` on the container sees nothing.

- **Keyboard focus requires explicit setup.** The container needs `setFocusPolicy(Qt::StrongFocus)` to accept keyboard focus at all.

### Floating Overlay Solution

Since we can't parent widgets on the native window container, the UI overlays (`FlightSettingsPanel`, `FlightInfoOverlay`, `NavWidget`) are implemented as **separate top-level frameless windows** (`Qt::FramelessWindowHint | Qt::Tool`):

- `Qt::Tool` keeps them out of the taskbar
- `WA_TranslucentBackground` allows transparent regions
- `WA_ShowWithoutActivating` prevents them from stealing focus
- `WA_TransparentForMouseEvents` (on the info overlay only) lets clicks pass through

Each overlay has a `positionOver()` method that places it relative to a global coordinate. `EarthViewPane` calls these whenever the earth pane moves.

### Overlay Position Tracking

`EarthViewPane` uses **ancestor event filters** to track position changes. On `showEvent`, it walks up the widget tree to the top-level `QMainWindow` and installs `eventFilter()` on every ancestor. The filter watches for `Move`, `Resize`, `WindowStateChange`, and `LayoutRequest` events and repositions all overlays.

### Ctrl+Drag Camera Rotation

The MapManipulator's built-in keybinding system doesn't work because the native window container doesn't forward keyboard events to the VSG event system.

The solution is a **QApplication-level event filter** installed by `EarthViewPane`. This is the only level that sees events from the native window. When it detects Ctrl+Left press, it enters a drag state and calls `MapManipulator::rotate()` directly with the mouse delta on each move event, consuming the events so Rocky doesn't also pan.

### Nav Widget Zoom

`MapManipulator::zoom()` uses the mouse cursor position as the zoom center (via `settings.zoomToMouse`). When triggered from a button click, the cursor is over the nav widget, not the earth. The fix is to call `MapManipulator::setDistance()` directly, which zooms toward/away from the view center point instead.

### Route Line Rendering

**depthOffset must be 0.** Rocky's `depthOffset` displaces geometry toward the camera in screen space to win depth tests against terrain. Large values (10,000-100,000m) cause the line to visually appear far from its actual position when viewed at close range. At 2500ft viewing distance, a 100km offset pushed lines completely out of view.

**Line geometry is updated in-place.** The route line entity is created once at startup via `FlightPlan::initRouteLine()`. When waypoints change, `updateRouteLine()` calls `geom.recycle(r)` to clear the geometry, repopulates the points, then calls `geom.dirty(r)` to trigger a GPU update. Destroying and recreating the entity doesn't work because Rocky's LineSystem requires an SRS reinitialization frame before processing new geometry.

### Altitude and Terrain

When terrain tiles haven't loaded yet, earth picks return the WGS84 ellipsoid height (which is negative in the northeastern US, ~-33m in Connecticut) instead of actual terrain elevation. `FlightPlan::getWpAltMSL()` clamps the result to a minimum of 1m above the ellipsoid to prevent lines from going underground.

## Rocky Build Notes

### ImGui Dependency

Rocky has compilation errors when built with `ROCKY_SUPPORTS_IMGUI=OFF` (forward declarations, unguarded LabelSystem code). Rather than patching the source, we build with ImGui enabled (the default). This pulls in the `imgui` vcpkg package with Vulkan bindings but eliminates the need for source patches.

### vsgQt is Not in Rocky's vcpkg Manifest

Rocky's `vcpkg.json` doesn't include vsgQt. The vcpkg `vsgqt` port tries to build its own Qt from source, which is extremely slow and fragile. The solution is to build vsgQt separately from source against the system Qt6 and vcpkg's VSG.

### Qt Advanced Docking System Version Detection

ADS uses `git describe` to determine its version number. Cloning with `--depth 1` strips tags, causing the CMake configure to fail with `VERSION "-128-NOTFOUND"`. Use a full clone (no `--depth 1`).

## Platform Quirks

### Wayland vs XCB

vsgQt creates Vulkan surfaces using `VK_KHR_xcb_surface`. If Qt defaults to the Wayland backend (when `WAYLAND_DISPLAY` is set), the Vulkan surface creation fails with `VK_ERROR_INITIALIZATION_FAILED`.

Fix: set `QT_QPA_PLATFORM=xcb` before running the application.

### Software Rendering (llvmpipe)

The application works on llvmpipe (Mesa's software Vulkan driver) but some optional Vulkan extensions produce harmless warnings:
- `VK_LAYER_KHRONOS_synchronization2` -- optional validation layer
- `VK_KHR_fragment_shader_barycentric` -- optional device extension

## File Architecture

```
src/main.cpp                 Wiring: creates Application, FlightPlan, handlers,
                             dock layout, connects signals, loads map JSON,
                             sets initial camera viewpoint

src/Geodesy.h                Pure math, no dependencies beyond glm.
                             Haversine distance, initial bearing, destination point,
                             great-circle interpolation with spherical slerp.

src/Waypoint.h               Data-only types. WaypointQueue is the thread-safe
                             bridge between VSG handlers and the Qt UI.
                             FlightSettings holds atomics for cross-thread config.

src/FlightPlan.h/.cpp        QObject owning the waypoint list and route line ECS
                             entity. initRouteLine() creates the entity at startup.
                             updateRouteLine() uses recycle()+dirty() to update
                             geometry in-place. Emits signals on changes.

src/WaypointHandler.h        VSG Visitor handling right-click (FlyTo) and
                             click-drag (Approach). Creates ECS point markers.
                             Reads FlightSettings, writes to WaypointQueue.

src/RockyViewer.h            Minimal vsgQt::Viewer subclass bridging Rocky's
                             frame() call with Qt's event loop.

src/widgets/
  EarthViewPane              Owns the vsgQt::Window, creates and positions all
                             floating overlays. App-level event filter for
                             Ctrl+drag rotation and ancestor tracking.

  FlightSettingsPanel        Frameless Qt::Tool window. Altitude/speed spinboxes,
                             mode selector, clear button. Writes to FlightSettings.

  FlightInfoOverlay          Frameless, mouse-transparent. Shows last waypoint.

  NavWidget                  Frameless. Grid of buttons emitting signals for
                             rotate/zoom/home, connected to MapManipulator in main.

data/default.map.json        Map layer configuration loaded at startup.
                             ArcGIS imagery + MapZen Terrarium elevation.
```

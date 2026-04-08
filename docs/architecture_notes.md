# Architecture Notes

## Overview

The application has three main layers:

1. **Rocky/VSG** -- Vulkan-based earth rendering running its own frame loop
2. **Qt6** -- Widget toolkit managing the window, docking, and UI overlays
3. **Bridge** -- vsgQt provides the `vsgQt::Window` (a `QWindow` subclass) that hosts the Vulkan surface inside a Qt widget hierarchy

## Threading Model

Rocky's rendering runs inside vsgQt's `Viewer::render()` method, which is called by a Qt timer. This is effectively single-threaded from Qt's perspective, but the VSG event handlers run during the frame call, not during Qt event processing.

Communication between the VSG event handler (which processes right-clicks) and Qt widgets (which display waypoint data) is done via a **thread-safe queue** (`WaypointQueue`). A 50ms Qt timer polls the queue and updates the UI.

Shared settings (altitude, speed, mode) use `std::atomic` variables in `FlightSettings`, written by Qt widget callbacks and read by the VSG event handler.

## Key Architectural Decisions

### Native Window Container Limitations

`QWidget::createWindowContainer()` wraps the vsgQt `QWindow` into a widget, but this creates a **native/foreign X11 window** that has significant limitations:

- **Child widgets don't render on top of it.** Qt paints child widgets into the parent's backing store, but the native window has its own surface. Overlaying QWidgets as children of the window container results in a black pane.

- **Widget-level event filters don't receive events.** Mouse and keyboard events go directly to the native X11 window, bypassing Qt's widget event dispatch. `QWidget::installEventFilter()` on the container sees nothing.

- **Keyboard focus requires explicit setup.** The container needs `setFocusPolicy(Qt::StrongFocus)` to accept keyboard focus at all, though even then, key events may not reach widget-level handlers.

### Floating Overlay Solution

Since we can't parent widgets on the native window container, the UI overlays (`FlightSettingsPanel`, `FlightInfoOverlay`, `NavWidget`) are implemented as **separate top-level frameless windows** (`Qt::FramelessWindowHint | Qt::Tool`):

- `Qt::Tool` keeps them out of the taskbar
- `WA_TranslucentBackground` allows transparent regions
- `WA_ShowWithoutActivating` prevents them from stealing focus
- `WA_TransparentForMouseEvents` (on the info overlay only) lets clicks pass through

Each overlay has a `positionOver()` method that places it relative to a global coordinate. `EarthViewPane` calls these whenever the earth pane moves.

### Overlay Position Tracking

`EarthViewPane` uses **ancestor event filters** to track position changes. On `showEvent`, it walks up the widget tree to the top-level `QMainWindow` and installs `eventFilter()` on every ancestor. The filter watches for `Move`, `Resize`, `WindowStateChange`, and `LayoutRequest` events and repositions all overlays.

This catches: window dragging, window un-maximize/restore, dock panel rearrangement, and splitter resizing.

### Ctrl+Drag Camera Rotation

The MapManipulator's built-in keybinding system (`bindMouse` with `MODKEY_Control`) doesn't work because keyboard events from the native window container don't reach the VSG event system through vsgQt's keyboard map.

The solution is a **QApplication-level event filter** installed by `EarthViewPane`. This is the only level that sees events from the native window. When it detects Ctrl+Left press, it enters a drag state and calls `MapManipulator::rotate()` directly with the mouse delta on each move event, consuming the events so Rocky doesn't also pan.

### Nav Widget Zoom

`MapManipulator::zoom()` uses the mouse cursor position as the zoom center (via `settings.zoomToMouse`). When triggered from a button click, the cursor is over the nav widget, not the earth. The fix is to call `MapManipulator::setDistance()` directly, which zooms toward/away from the view center point instead.

## Rocky Build Quirks

### ImGui Dependency

Rocky has compilation errors when built with `ROCKY_SUPPORTS_IMGUI=OFF` (forward declarations, unguarded LabelSystem code). Rather than patching the source, we build with ImGui enabled (the default). This pulls in the `imgui` vcpkg package with Vulkan bindings, which adds ~1MB to the build but eliminates the need for source patches.

### vsgQt is Not in Rocky's vcpkg Manifest

Rocky's `vcpkg.json` doesn't include vsgQt. The vcpkg `vsgqt` port tries to build its own Qt from source (via the vcpkg `qtbase` port), which is extremely slow and fragile. The solution is to build vsgQt separately from source against the system Qt6 and vcpkg's VSG.

### Qt Advanced Docking System Version Detection

ADS uses `git describe` to determine its version number. Cloning with `--depth 1` strips tags, causing the CMake configure to fail with `VERSION "-128-NOTFOUND"`. Use a full clone (no `--depth 1`).

## Platform Quirks

### Wayland vs XCB

vsgQt creates Vulkan surfaces using `VK_KHR_xcb_surface`. If Qt defaults to the Wayland backend (when `WAYLAND_DISPLAY` is set), the Vulkan surface creation fails with `VK_ERROR_INITIALIZATION_FAILED` ("no suitable Vulkan PhysicalDevice available").

Fix: set `QT_QPA_PLATFORM=xcb` before running the application.

### Software Rendering (llvmpipe)

The application works on llvmpipe (Mesa's software Vulkan driver) but some optional Vulkan extensions produce harmless warnings:
- `VK_LAYER_KHRONOS_synchronization2` -- optional validation layer
- `VK_KHR_fragment_shader_barycentric` -- optional device extension

## File Architecture

```
main.cpp                 Wiring: creates Application, FlightPlan, handlers,
                         dock layout, connects signals, runs event loop

Geodesy.h                Pure math, no dependencies beyond glm.
                         Haversine distance, initial bearing, destination point,
                         great-circle interpolation with spherical slerp.

Waypoint.h               Data-only types. WaypointQueue is the thread-safe
                         bridge between VSG handlers and the Qt UI.
                         FlightSettings holds atomics for cross-thread config.

FlightPlan.h/.cpp        QObject owning the waypoint list and route line ECS
                         entity. Emits signals on changes so the UI can react.

WaypointHandler.h        VSG Visitor handling right-click (FlyTo) and
                         click-drag (Approach). Creates ECS point markers.
                         Reads FlightSettings, writes to WaypointQueue.

RockyViewer.h            Minimal vsgQt::Viewer subclass bridging Rocky's
                         frame() call with Qt's event loop.

widgets/
  EarthViewPane           Owns the vsgQt::Window, creates and positions all
                          floating overlays. App-level event filter for
                          Ctrl+drag rotation and ancestor tracking.

  FlightSettingsPanel     Frameless Qt::Tool window. Altitude/speed spinboxes,
                          mode selector, clear button. Writes to FlightSettings.

  FlightInfoOverlay       Frameless, mouse-transparent. Shows last waypoint.

  NavWidget               Frameless. Grid of buttons emitting signals for
                          rotate/zoom/home, connected to MapManipulator in main.
```

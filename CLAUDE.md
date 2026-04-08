# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

```bash
cmake --preset default              # configure (uses CMakePresets.json for paths)
cmake --build --preset default      # build
ctest --preset default              # run all 21 unit tests
scripts/test.sh                     # alternative test runner
scripts/test.sh -R Geodesy          # run subset of tests by name
scripts/run.sh                      # run the application
docker/build.sh                     # full clean build test in Docker (Ubuntu 24.04)
```

After editing CMakePresets.json paths for your system, do a one-time `cmake --preset default`, then iterate with `cmake --build --preset default`.

## Architecture

This is a C++17/Qt6 flight planner with a Vulkan earth renderer (Rocky) embedded in a Qt Advanced Docking System layout.

**Threading model:** Rocky's frame loop runs inside a Qt timer via `RockyQtViewer`. The VSG event handler (`WaypointHandler`) runs during the frame call, not during Qt event processing. A thread-safe `WaypointQueue` bridges VSG→Qt. Shared config uses `std::atomic` fields in `FlightSettings`. A 50ms Qt timer in `main.cpp` polls the queue.

**Key constraint — native window container:** `QWidget::createWindowContainer()` wraps the Vulkan `QWindow` but creates a native X11 window that:
- Cannot have Qt child widgets painted on top (they render black)
- Does not forward mouse/keyboard events to Qt widget-level event filters
- Requires `QApplication::installEventFilter()` to intercept events (see `EarthViewPane`)

This drives the **floating overlay pattern**: `FlightSettingsPanel`, `FlightInfoOverlay`, and `NavWidget` are separate frameless `Qt::Tool` windows positioned over the earth pane. `EarthViewPane` tracks position via ancestor event filters (installed on every parent widget up to the main window).

**Ctrl+drag rotation** is handled by a `QApplication`-level event filter in `EarthViewPane` that calls `MapManipulator::rotate()` directly, since keyboard events don't reach the VSG binding system through the native container.

## Key Files

- `src/main.cpp` — wiring: creates all objects, connects signals, sets up docks
- `src/FlightPlan.h/.cpp` — QObject owning waypoints + ECS route line entity, emits signals on changes
- `src/WaypointHandler.h` — VSG Visitor for right-click (FlyTo) and click-drag (Approach to Hover)
- `src/Geodesy.h` — pure math, no external deps beyond glm. All functions are `inline` in namespace `geo`
- `src/widgets/EarthViewPane.cpp` — the most complex widget: manages vsgQt window, floating overlays, app-level event filter

## Rocky/VSG Patterns

- ECS entity creation: `app.registry.write([&](entt::registry& r) { ... })` for thread-safe access
- Point/Line/Mesh components each have Geometry + Style + Component triples
- `rocky::pointAtWindowCoords(viewer, x, y)` returns `Result<DisplayGeoPoint>` — check `.ok()` then `.value().point`
- Line geometry points use `(lon, lat, alt)` order (matching Rocky's convention), but `interpolateGreatCircle()` takes `(lat, lon, alt)` parameter order — watch for swaps
- `app.vsgcontext->requestFrame()` must be called after ECS changes to trigger a render

## Platform Notes

- `QT_QPA_PLATFORM=xcb` is required — vsgQt creates XCB Vulkan surfaces, but the system may default to Wayland
- Rocky is built with ImGui enabled (the default) to avoid source patches needed when `ROCKY_SUPPORTS_IMGUI=OFF`
- Qt Advanced Docking System needs a full git clone (not `--depth 1`) for version tag detection
- Nav widget zoom uses `MapManipulator::setDistance()` instead of `zoom()` to avoid mouse-position-based centering

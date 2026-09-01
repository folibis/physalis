# CLAUDE.md

## What this is

**Physalis** — a Qt6 desktop application for building 2D physics scenes and
running them. You draw shapes, group them into bodies, connect them with
joints, add rays, sensors and explosions, and write rules that fire on events
("when the basket touches the left wall, reverse the motor"). Scenes are saved
as `*.phys` (JSON).

The simulation itself is not part of the application. It lives in a **plugin**.

## The one rule that shapes everything: the app knows no physics

The application must never know what a specific physics engine offers. It does
not know what a "friction" or a "prismatic joint" or a "contact begin" is. It
asks the loaded plugin for:

- body and shape properties (`IPhysicsEngine::bodyProperties()`, `shapeProperties()`)
- joint types and their parameters (`jointTypes()`)
- events a rule can watch (`bodyEvents()`, `jointEvents()`)
- actions a rule can perform (`bodyActions()`)

and builds its property panes, rule menus and combo boxes from the answers.
Adding a joint type to the Box2D plugin makes it appear in the UI with no
application change. **If you find yourself adding a physics term to `src/`,
stop — it belongs in a plugin catalogue.**

## Layout

```
include/          the plugin contract, shared by app and engines
    PluginApi.h       the ABI: signature, entry points, PHYSALIS_DECLARE_ENGINE
    IPhysicsEngine.h  what an engine must implement
    PhysicsTypes.h    WorldDesc, BodyDesc, ShapePart, Geometry, RayHit
    JointTypes.h      JointType/JointParam descriptors
    EngineRegistry.h  discovery of plugins
src/              the application (Qt widgets, canvas, panels, serialization)
    CanvasScene       the scene: shapes, bodies, joints, rays, explosions,
                      selection, drag modes, painting
    ShapeItem + RectangleItem/CircleItem/PolygonItem   the drawable shapes
    PhysicsBody, Joint, RayItem, ExplosionItem
    Rule.h            one rule: condition, action, value source
    RulesPanel        the rule cards
    PropertyPanel + PropertyPane/*   property tables, one pane per selection kind
    SimulationController   drives the engine, polls events, applies rules
    SceneSerializer   *.phys read/write
    MainWindow, UndoStack, OptionsDialog, AboutDialog, SceneTree
ui/               .ui forms (AUTOUIC searches here, not beside the sources)
resources/        icons (physalis.svg), the .rc that gives the exe its icon
engines/box2d/    one plugin: its own CMakeLists, include/ and src/
tests/            one .cpp per scenario, all built into a single binary
deploy/           the *.phys file association template
cmake/            Version.h.in
```

## Building and testing

Qt 6.5.1 MinGW; Ninja; C++17.

```bash
cmake -S . -B build
cmake --build build
ctest --test-dir build
```

Run one test: `ctest --test-dir build -R RuleSource --output-on-failure`, or
`build/PhysalisTests.exe --gtest_filter=RuleSource.*` with the Qt bin directory
on `PATH`.

**On Windows the application locks its own executable.** If the link fails with
`cannot open output file Physalis.exe: Permission denied`, the app is running.
Ask the user to close it — do not kill it, they may have unsaved work.

## The test suite

- **One binary**, `PhysalisTests`, built from a glob of `tests/*.cpp`. Adding a
  test is dropping a file in; no CMake edit.
- `tests/QtTestMain.cpp` provides `main()` — a `QApplication` must exist before
  any widget, and GoogleTest's own main does not create one.
- CTest lists each case separately via `gtest_discover_tests(... PRE_TEST)`.
  Discovery runs the binary, so every DLL it needs must sit beside it (this is
  why `Qt6Test.dll` is copied there).
- **Tests must not read or write external scene files.** Build the scene in
  code. `tests/SceneFixtures.h` has helpers.
- Every test runs with `PHYSALIS_SETTINGS` pointing at its own INI, so a test
  can never read or clobber the user's real settings. Two behaviours key off
  that variable being set: the unsaved-changes prompt on close stands aside
  (a modal dialog would hang the run).
- No `printf` for narration. Assertions carry their own messages.

Since it is one process now, a crash takes the whole run with it — a test that
dies shows as `[ RUN ]` with no result line.

## Plugins

A plugin is a shared library named `physalis_*.dll|so|dylib` sitting **beside
the executable**. `EngineRegistry` scans that directory at startup, checks the
`physalisEngineApi` signature (`PhysalisEngine-1`), and keeps what matches.
`PHYSALIS_DECLARE_ENGINE(NAME, VERSION, TYPE)` in `PluginApi.h` writes the
entry points.

Inside `engines/box2d/`, the catalogue files (`Box2DCatalogue.cpp`,
`Box2DJointTypes.cpp`) are pure description — what the app is allowed to show
and which keys are settable while running. The rest does the work.

## Things that have bitten before

- **`PhysalisCore` is an OBJECT library, not STATIC.** A static library lets the
  linker drop the compiled `.qrc`, and every icon comes out empty.
- **Scale.** `pixelsPerMeter` is a world setting; forces and gravity are scaled
  by `50 / pixelsPerMeter`. At the common `ppm: 1000` that is a factor of 0.05,
  so sensible-looking force values do nothing and tiny ones are huge.
- **Speculative contacts.** Box2D reports a touch about `4 × linearSlop` early —
  ~20 scene px at `ppm: 1000`. Things react before they visually touch, and a
  body starting exactly that far from a wall raises a contact on frame 0.
- **`collideConnected`.** A joint disables collision between the two bodies it
  connects. If one of them is scenery, the other passes straight through it.
- **Contact events are auto-enabled** for any shape named as a rule subject
  (`SimulationController.cpp`), so turning the flag off in the file changes
  nothing for that shape.
- **Rebuilding widgets from their own signal handler crashes.** The rules panel
  and the property panes both rebuild controls in response to a combo box
  changing — that deletes the sender mid-signal. Queue it:
  `QMetaObject::invokeMethod(this, [...]{...}, Qt::QueuedConnection)`.
  See `RulesPanel::scheduleValueEditorRefresh`.
- **Setting a body's position teleports it.** `b2Body_SetTransform` bypasses the
  solver: no velocity, no contacts on the way, overlapping shapes left behind.
  To move something smoothly, drive a joint instead (a motor joint's
  `linearOffsetX`, a prismatic's `targetTranslation`).
- **`stop()` restores the snapshot**, so reading positions after it gives you
  the pre-run state.

## Versioning

`x.y` comes from `project(Physalis VERSION ...)`; `z` is a build counter that
increments on its own. **Only Release-type builds stamp it** — a Debug build
writes `Version.h` once at configure time, so Build → Run does not rebuild the
world. `Version.h` is included by `main.cpp` only, never by `PhysalisCore`.

## Style

Match the surrounding code. Comments explain *why*, in prose, and are worth
writing only where the reason is not on the line itself — several of the
"things that have bitten before" above are recorded as comments at the place
they matter. No decorative headers, no restating the code.

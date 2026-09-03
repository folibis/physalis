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
- what the running world itself will still answer to (`worldProperties()`)
- joint types and their parameters (`jointTypes()`), and what a joint measures
  once it exists (`jointReadables()`)
- events a rule can watch (`bodyEvents()`, `shapeEvents()`, and each joint
  type's own `events`)
- actions a rule can perform (`bodyActions()`, `jointActions()`)

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
    SceneExporter     finds export converters and runs one
    MainWindow, UndoStack, OptionsDialog, AboutDialog, SceneTree
ui/               .ui forms (AUTOUIC searches here, not beside the sources)
resources/        icons (physalis.svg), the .rc that gives the exe its icon
engines/box2d/    one plugin: its own CMakeLists, include/ and src/
tests/            one .cpp per scenario, all built into a single binary
exporters/        export converters -- one folder each, all JavaScript
    box2d-qt-project/  manifest.json, export.js, templates/ (a whole runnable
                       Qt + Box2D project, rules included)
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
  To move something smoothly use *Glide To X/Y* (`b2Body_SetTargetTransform`,
  which sets the velocity that arrives there by the end of the step), or drive
  a joint instead (a motor joint's `linearOffsetX`, a prismatic's
  `targetTranslation`).
- **`EngineEvent` is not safe to build positionally.** Two shape names sit
  between the handles and `eventId`, so `{joint, body, other, "limitLower"}`
  puts the id in `subjectShape` and the event reaches no rule at all. Joint
  limit events were dead this way for a while, silently. Assign the fields.
- **Torque and angular impulse come down by the scale twice.** A torque is a
  force times a distance and both are quoted in scene units, so `setBodyParam`
  divides by `pixelsPerMeter²` where a linear impulse divides by it once.
  Without that, a number that looks sensible next to an impulse is a million
  times too large.
- **Joint limits lose to an overpowered motor.** They are constraints, not
  walls; a `maxMotorForce` far beyond what the bodies weigh drives straight
  through one. At `ppm: 1000` a 40×40 box weighs about two grams, so tenths of
  a newton are already generous.
- **`stop()` restores the snapshot**, so reading positions after it gives you
  the pre-run state.

## Exporting

**File -> Export to -> ...** is built from whatever converters are found under
the folder named in Options -> Common -> Converters folder. A converter is a
folder holding a `manifest.json` (name and description) and an `export.js`
defining `exportScene(scene, io)`; anything else beside them -- templates, a
README -- is the converter's own business.

The same rule as the plugins applies, one step further out: **the application
converts nothing and knows no formats.** It hands over the scene and two
functions, and everything else is the script's.

- `scene` is the saved `.phys` document -- shapes, bodies, joints, rules,
  rays, explosions, world, field -- plus `scene.simulation` (bodies and joints
  as the *engine* receives them: transforms resolved, geometry in body-local
  coordinates, which a saved file does not carry) and `scene.engine` (the
  loaded engine's catalogue) and `scene.settings` (the application's own
  preferences, grouped as the INI stores them -- the physics view's colours
  live there rather than in the scene).
- `io.read(path)` reads from the converter's own folder and nowhere else;
  `io.write(path, text)` writes into the folder the user chose and nowhere
  else. Both are path-jailed, and `QJSEngine` opens no other door.
  `io.log(text)` says something for the finished message to carry -- only the
  converter knows what it did.

A run always ends in a dialog: what was written and where, or why it failed
and on which line. A converter that writes no files has not converted
anything, whatever it thinks, and is reported as a failure.

A converter may also declare **its own settings** in the manifest -- name,
label, type (`string`, `int`, `double`, `bool`, `color`, `choice`, `path`,
`file`), default, range. A type this build has never heard of falls back to a
text box, so a converter written against a later one is still usable. The app renders them on an **Options -> Export** tab, one page per
converter, stores them under `Export/<folder name>` in the same INI as
everything else, and hands each converter its own resolved values as
`scene.converterSettings` with defaults already applied. It never interprets
one -- the same bargain it has with the engine catalogues.

Discovery parses manifests and never evaluates a script: opening the File menu,
or the Options dialog, is not consent to run somebody's code -- which is why
the settings are declared in the manifest and not in `export.js`.

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

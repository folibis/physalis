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
  type's own `events`) -- including whether each one happens *with* something
  (`EventType::namesOther`), which is what decides if the rule card asks which
  other object it was
- actions a rule can perform (`bodyActions()`, `jointActions()`)

and builds its property panes, rule menus and combo boxes from the answers.
Adding a joint type to the Box2D plugin makes it appear in the UI with no
application change. **If you find yourself adding a physics term to `src/`,
stop — it belongs in a plugin catalogue.**

**The document is the same bargain.** A body, a shape and the world carry a
`QVariantMap params` keyed by the engine's own property names, and nothing else
of physics: `BodyDesc` has a transform, a type and its parts; `ShapePart` has a
name and geometry; `WorldDesc` has `pixelsPerMeter`, the scene's own scale.
Every stored value comes from a catalogue entry marked `stored`, which carries
its default -- and the editor keeps only what differs from that, so an engine
that drops or renames a property leaves nothing stale behind. `*.phys` files
write those maps out under the engine's names, in a `physics` block per body,
per shape and on the world.

Two properties the *editor* also has to recognise, because it draws with them:
whatever the engine tags `PropertyRole::Sensor` (hatched rather than filled) and
`PropertyRole::Density` (where a body balances). It asks for the key by role --
`CanvasScene::isSensorShape`, `shapeDensity` -- and still names neither.

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
    FullScreenView    a second view onto the scene, for a run on its own screen
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
engines/chipmunk/ another, on Chipmunk2D -- Box2D's keys and units wherever
                  Chipmunk can honour them, its own constraints as joint types
tests/            one .cpp per scenario, all built into a single binary
exporters/        export converters -- one folder each, all JavaScript
    box2d-qt-project/  manifest.json, export.js, templates/ -> one plain Box2D
                       program, main.cpp; rules become ifs after b2World_Step
    planck-js/         the same, as one index.html of plain Planck.js
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

**A scene is built for one engine and keeps it.** Engines differ in what they
offer -- their joint types, and part of their body and shape properties -- so
there is no chooser on the toolbar: switching one under an existing scene would
silently drop half of it. Which engine a *new* scene gets is an Option
(Common -> Physics engine, stored as `Engine/default`); the name is written into the
`*.phys`, and a file naming an engine that is not installed is refused with a
message rather than half loaded. Changing the engine in Options is the one
setting that reaches the open scene: `MainWindow::adoptEngine` offers to save
it, closes it, and starts an empty one on the new engine -- keeping the scene
keeps its engine. The body and shape property tables show only the rows the
scene's engine publishes -- see `dropRowsTheEngineIgnores` in
`PhysicsPropertyPane.cpp`.

**Joints are coloured and drawn by kind, not by type.** A type id belongs to one
engine (`revolute` is Box2D's, `pin` is Chipmunk's); the five `JointVisual`
kinds every engine tags its types with -- pivot, segment, axis, rigid, link --
belong to all of them. Options carries one colour and one line style per kind
(`jointKindColors` / `jointKindStyles` in the INI), so the list does not grow a
row per engine and a scene keeps its look whichever engine draws it. `Rod` is
the waisted shaft joints have always been drawn with; the other styles stroke
the line between the anchors instead.

Inside `engines/box2d/`, the catalogue files (`Box2DCatalogue.cpp`,
`Box2DJointTypes.cpp`) are pure description — what the app is allowed to show
and which keys are settable while running. The rest does the work.

**What a run shows is a list of layers**, not one debug switch:
`CanvasScene::RunLayer` -- grid, joints, body axes, rays, explosions, and the
sleep shading that tints bodies by whether the solver still has them awake --
set from the toolbar's view list (`ViewLayersCombo`). They apply to a *run* and
nothing else: while editing the canvas draws all of it, because that is when
joints and rays are being placed and a switch that hid them would make them
unusable. Ask `layerVisible()` at a paint site, never the switch itself. The
INI key for the shading is still `debugView`, which is what the single switch
these grew out of was called.

**The slingshot** flings a body with the mouse during a run: press on it, pull
back, let go, and it is pushed the other way through its centre of mass. Whether
a body can be shot, its max power and how far full pull is belong to the body
(`ShotSettings` on `PhysicsBody`, the "Can Be Shot" rows, a `shot` block in the
`*.phys` only when set); how the pull line is drawn -- its colours, width and
style -- is an application setting (Options -> Physics -> Slingshot). The push is the engine's: each tags its
impulse properties `PropertyRole::ImpulseX`/`ImpulseY`, and
`SimulationController::shoot` finds them by role. The gesture lives on
`CanvasScene` (`beginShot`/`aimShot`/`releaseShot`/`cancelShot`) because the
full-screen view is not interactive and never lets the scene see its mouse --
it calls the same four itself.

**Shapes lying over one another** are reached with Ctrl+click, in Edit and
Physics mode alike: it steps down through `CanvasScene::shapesAt` -- everything
under the pointer, top first -- from the current selection to the shape beneath,
and round to the top again. A plain press on the shape already selected keeps
it even where another lies on top, so a shape reached that way can still be
dragged or double-clicked into a body (Ctrl+double-click makes that body
static). The slingshot looks through shapes that
cannot be shot to the topmost one that can.

**A removal can be answered.** Before a rule's action takes a body away -- an
engine marks such actions `ActionType::removesBody` -- `SimulationController`
looks for rules on that body (or its shapes) watching the application's own
event `Rule::aboutToBeRemovedEvent()`. If there are any, they are carried out
instead and the body stays; if not, it is removed. "The other object" in an
answer is the subject of the rule that did the removing. An answer that itself
removes the body really removes it: `m_handlingRemoval` stops it asking again.

**The world raises one event, the application's own:** `Rule::runStartedEvent()`,
"starting simulation", carried out once in `start()` with the world built and
the snapshot taken, before the first step -- so a rule can set things up, and
Stop still puts back what it changed.

**"Init state" is the application's own action on a body:** back where it stood
when the run started (taken in `start()`, before the start rules), facing the
same way, and not moving. The engine's position, angle and velocity properties
are found by role -- `PropertyRole::PositionX` and the rest.

**"Clone" is the application's own action on a body** (`Rule::cloneAction()`,
X and Y in `actionParams`): a copy of every shape, through the same JSON as
Duplicate, posed as the run's snapshot found the parent -- so a removed body
can still be cloned -- and moved so its origin lands on X, Y, then added to the
world. Clones are run state: `PhysicsBody::isRunOnly()`, left out of a save,
made and destroyed without `bodiesChanged` (every panel listing bodies would
rebuild per clone), and deleted by `stop()`. Rules name no clone.

**Read-only rows are there before a run too.** They read what the object
starts with: `SimulationController::initialValue` builds the scene into a
world it never steps (`buildWorld`, the same code `start()` uses), reads it, and
drops it at the end of that pass of the event loop so it never answers for a
scene that has since changed. A reading an engine marks `rulesOnly` -- bounds,
centre of mass, last hit, position and angle (edited on the canvas), a circle's
radius, a sensor's count -- stays out of the table and in the rule menus.

**Two world actions are the application's own**:
`Rule::stopRunAction()` and `holdRunAction()`, offered on the world because
that is what a rule names when it means the simulation itself. No engine knows
a run is being watched, let alone how to end one. They cannot be carried out
where a rule fires -- that is inside the step, with the solver on the stack --
so `SimulationController` remembers one and acts on it once the step is over.

## Things that have bitten before

- **`PhysalisCore` is an OBJECT library, not STATIC.** A static library lets the
  linker drop the compiled `.qrc`, and every icon comes out empty.
- **Scale.** `pixelsPerMeter` is a world setting; forces and gravity are scaled
  by `50 / pixelsPerMeter`. At the common `ppm: 1000` that is a factor of 0.05,
  so sensible-looking force values do nothing and tiny ones are huge.
- **Box2D's length unit.** Box2D's tolerances are lengths fixed for metre-sized
  objects: 5 mm of slop, and a contact is made (and "begins contact" reported)
  once shapes are within `4 × linearSlop` = 2 cm. At `ppm: 1000` that is 20 scene
  px, so rules fired visibly before shapes touched. `createWorld` calls
  `b2SetLengthUnitsPerMeter(50 / ppm)` -- the same reference the pace is quoted
  at -- so the tolerances are what they would be at 50 px per metre. It is a
  global, set before each world is made; the C++ export does the same.
- **`collideConnected`.** A joint disables collision between the two bodies it
  connects. If one of them is scenery, the other passes straight through it.
- **Contact events are auto-enabled** for any shape named as a rule subject:
  `SimulationController` sets `ShapePart::watchedByRules`, and each engine
  switches on whatever it needs to report contacts -- so turning the flag off
  in the file changes nothing for that shape.
- **A sensor needs both sides to opt in.** Box2D 3.1 reports an overlap only
  when the sensor *and* the shape entering it have `enableSensorEvents`, and it
  is off by default even for sensors -- so a pocket's rule never fired unless
  every ball had been ticked by hand. Once a rule watches a sensor, the engine
  gives every shape in the world sensor events, the ones already made and the
  ones still to come. And a sensor never raises *contact* events at all: a rule
  on one wants "is entered", not "begins contact".
- **A property the editor shows is one the engine published.** Panes are built
  by `rowsFromCatalogue` (stored values, editable) and `liveRowsFromCatalogue`
  (what a run answers, read-only). A row the editor owns -- a name, the body
  type, Enabled -- carries no engine key, and nothing else in `src/` may.
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
  a newton are already generous. Pushed far enough it stops being a bad-looking
  run and becomes a broken one -- see the two entries below, which came out of
  exactly that.
- **A sliding joint's travel is measured from where it starts.** Box2D measures
  a prismatic (and wheel) joint between its two anchors, so a joint whose
  anchors sit 400 apart *starts* at 400 -- and limits written in the editor as
  "0 to 400 units of travel" would then sit entirely behind it, with the motor
  grinding against a limit it began the wrong side of. The editor means travel
  from where the joint starts, so `Box2DEngine::m_travelOrigins` keeps that
  offset per joint: added to the limits (and `targetTranslation`) going in,
  taken back off `translation` coming out. It is zero for the usual case of two
  anchors dropped on the same point, and for every other kind of joint.
  The picture follows the same coordinate: `CanvasScene` hangs the travel band
  off **anchor B**, since zero is where that anchor stands, and drawing it from
  anchor A put the whole range somewhere the joint could never reach as soon as
  the two anchors were apart.
- **An engine may not end the process.** Box2D checks its own arithmetic and,
  left alone, calls `abort()` when a check fails -- which used to take the
  editor down mid-run, unsaved work and all. `createWorld` installs
  `b2SetAssertFcn(rememberAssertion)`, which keeps the message and returns zero
  (Box2D's "do not break"), and `collectWreckage()` after every step disables
  any body the solver left holding a value that is no longer a number, naming
  it once. Both come back through `IPhysicsEngine::takeProblems()` -- cleared by
  the asking -- which `SimulationController` collects and the window shows in
  place of running/paused. A scene is *allowed* to ask for the impossible; what
  it gets is a sentence, not a crash.
- **Playing faster never means stepping bigger.** The toolbar's speed chooser
  (×¼ to ×8) multiplies the *wall-clock time* handed to
  `SimulationController::advance`, and the solver keeps the same `timeStep()` --
  a longer step is a different simulation, not a faster one. The catch-up
  ceiling scales with it (`kMaxStepsPerTick × speed`), or ×4 would run
  at ×1 and drop the rest. `advance()` exists so a test can hand over the
  time a tick would have carried instead of waiting for it.
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

## Packaging

`cpack` builds from whatever is installed by `cmake --install`, so both start
from the same tree: the program, the engine plugins beside it, and the export
converters under `exporters/`. Build Release first -- a Debug executable is
some forty megabytes of symbols.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cd build && cpack
```

- **Windows** carries the Qt runtime. `windeployqt` runs against the *installed*
  executable at install time and lands everything beside it, which is the
  layout Qt finds without a `qt.conf`. Qt Quick, the on-screen keyboard and the
  translations are skipped: QJSEngine is the only part of Qml the program uses,
  and there is no QML in it. The generators are `NSIS;ZIP` where `makensis` is
  on the path and `ZIP` alone where it is not -- the archive is the same tree,
  unpacked.
- **Linux** carries none of Qt. `CPACK_DEBIAN_PACKAGE_SHLIBDEPS` reads what the
  program and the plugins actually link and writes the dependencies from the
  packages providing them, so the list cannot drift from the build. The program
  goes in `<libdir>/physalis` with its plugins, a link from `bin/physalis`
  points at it (Qt resolves that before it looks for plugins), and the desktop
  entry, icon and `*.phys` MIME description go where a desktop expects them.
- **The `*.phys` association on Windows** is still the two `.reg` files in
  `deploy/`, not something the installer writes: NSIS would have to carry the
  same registry keys as escaped script, and a mis-escaped one writes a broken
  association rather than none.
- Box2D is fetched with `EXCLUDE_FROM_ALL` so its own install rules -- its
  static library and headers -- stay out of a Physalis package.

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

# CLAUDE.md

## What this is

**Physalis** — a Qt6 desktop application for building 2D physics scenes and
running them. It allows drawing shapes, grouping them into bodies, connecting
those bodies with joints, adding rays, sensors and explosions, and writing
rules that fire on events. Scenes are saved as `*.phys` (JSON).

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

**The scene's own variables are the one value it carries that no engine
named.** A `SceneVariable` is a name, a type (bool, int, double) and the value
it starts a run at; rules read and write them, and the log shows them. They are
addressed as properties of one pseudo-object, `Rule::variables()`, the way
elapsed time and the frame count are properties of the world -- so a scene with
twenty of them still puts one entry in the object lists. `RulesPanel::describe`
hands back a synthesised `physics::JointParam` for each, which is how the rule
cards build an editor for something the catalogue never mentioned. They are run
state: started from what the scene declares and gone when the run stops, like
every position on the canvas.

**A control is the scene's own too, and it has an id and a caption.** A
`SceneControl` is a box to tick, a button or a slider, drawn under the caption
while a run is going and nowhere else. The id is how a rule addresses it and
never moves under one; the caption is only what it says, defaults to the id,
and a rule may change it mid-run -- which is why identity could not be the
words on a button. Rules reach every control through one pseudo-object,
`Rule::controls()`, the way they reach every variable.

**A key is named, never numbered.** `Rule::chordName` writes what the editor
writes on a menu -- "Enter", "Up", "Ctrl+Shift+A" -- and a rule stores and
compares that text. A raw key code would not survive a different Qt, and would
mean a scene asking for Ctrl+A could be fired by a bare A. The canvas reserves
nothing of its own -- the pause, step and reset keys it used to keep are gone,
because a scene that wants the space bar should have it.

What a scene has to share is a chord the menu bar holds. Most are no clash:
`m_lockedWhileRunning` disables the editing actions while a run is going, which
is the only time a rule is watching. **The transport is the exception** -- F5,
F6 and F8 stay live during a run by design, so a scene binding one of those
meets Qt's shortcut map, which consumes a matched chord before the focused
widget's `keyPressEvent` and therefore before `keyPressedDuringRun`. That is
read off Qt's documented order and is *not* under test: `QTest::keyClick` posts
to the widget and skips the window's shortcut map, so a test of it measures the
instrument rather than the behaviour. If a scene ever needs those keys, move
the transport rather than trusting this paragraph.

`FullScreenView` keeps only Escape and forwards everything else to the scene --
it is not interactive, so it hands the events over itself rather than letting
the view do it.

**A selection spans kinds.** Shapes, rays and explosions are picked together
-- `m_physicsSelection`, `m_raySelection`, `m_explosionSelection`, each a list,
the first of each leading: it is what a property pane that can only show one
shows and what names the undo step, the way the active shape does for shapes.
`selectedRay()` and `selectedExplosion()` are those leads, which is why the
forty-odd places that ask for one still read as they did.

Picking one kind used to let the others go -- `letOthersGo` dropped all three
but the one being picked -- so a box over two blasts, two rangefinders and a
body could keep only the topmost of one kind, and the box only ever looked for
`ShapeItem` in the first place. The exclusivity is gone from all but the
**joint**, which is a linkage between two things, has no position of its own
and a pane that answers for nothing else; picking one drops the rest and
picking any of the rest drops it.

What replaces it is that **replacing a selection is the caller's business**,
since only the caller knows whether Shift was held: a press without it calls
`clearCanvasSelection` (or clears the kinds it is about to set), and a sweep in
Physics mode states all three outright -- `applyRubberBand` sets the rays, the
blasts and the shapes it caught, clearing a kind it caught none of. Leaning on
the press to have cleared the shapes instead left a body picked behind a box
drawn round two rangefinders. Edit mode is the exception and still adds
unconditionally, because rays and blasts are not on the canvas there and the
press has already cleared what it had to.

Everything that acts on a selection acts on all of it: Delete, the arrow keys,
a drag (`carryPickedForDrag` carries every picked piece, measuring each one's
move from the grabbed point, whichever kind that was), Copy and what a paste
leaves picked. A click that does not move narrows a group to the one thing
under the pointer, for a ray and a blast as it already did for a shape.

**Picking a group of shapes has one owner**, `CanvasScene::selectShapes` --
Select All, a swept box and what a paste leaves picked all go through it.
Each of the three used to loop over `selectForPhysics` or
`addToEditSelection`, both of which repaint the scene and make every panel
watching the selection rebuild: once per shape, so 400 of them measured 6.9 s
and 399 signals for a box, and 400 signals for a paste. The work is done first
and the signal goes out once. Membership while the list is built is asked of a
`QSet`, not of the vector being appended to, which is what keeps it linear.
A box in Edit mode always adds, because the press that started it has already
cleared the selection unless Shift was held.

**Select All is the scene's, not the menu's** (`CanvasScene::selectAllShapes`).
Edit mode keeps one active shape with the rest co-selected beside it and
Physics mode keeps its own pick, in which naming a shape names the whole of its
body -- so "all of it" means two different things and the scene is what knows
which. The active shape is deliberately not in `m_editSelection`, which is why
asking that list for a count gives one less than there are.

**Which shape is drawn over which is the scene's, and the file carries it.**
`CanvasScene::restackShapes` renumbers every shape's `zValue` to its index in
the new order rather than nudging the picked ones, so the values stay small and
the order stays exact however often it is done; `shapesInStackOrder` asks
`items(Qt::AscendingOrder)` rather than sorting, since that already settles
shapes sharing a z by the order they were added -- which is how they are
painted. `z` is written to the `*.phys` only when it is not zero, so a scene
that has never been restacked reads exactly as it did before stacking existed.
A move that changes nothing returns false, which is what keeps an undo state
off the stack for it.

**Names on the canvas are drawn in device pixels, not scene units**
(`CanvasScene::drawObjectNames`, off by default, `Panels/showObjectNames`).
The painter's transform is reset and each anchor mapped by hand, so a label is
the same size at 50% and at 500% -- the same bargain the log and the controls
make by being widgets over the canvas. A body speaks for its shapes: labelling
both would put two labels on one drawing, and the body is what a rule usually
names. It is deliberately **not** a `RunLayer`: those are all on while editing,
and this one has to default off, since a field of labels is in the way of the
drawing most of the time.

**Measuring is the canvas's, and is not the scene.** `CanvasScene::Measure`
-- a distance or an angle -- puts points in `m_measurePoints` and nowhere
else: no item, no undo step, nothing in the file. They are drawn in device
pixels by `drawMeasurement`, for the same reason the names are, and each label
stands off along its own direction by the size of its box, because a fixed
offset put it on the line it was reading. The tool takes the left button
before anything else on the canvas, the slingshot included, since it was
picked on purpose. A point is pulled to a corner, a centre, a centre of mass
or an anchor within 8 device pixels, and otherwise to the grid. A distance is
written in the rulers' unit (`setMeasureUnit`, set where the rulers are),
to a step of one device pixel. The window applies the checked tool to the
scene and to a run's copy alike.

**The canvas's right-click menu carries the canvas's own commands**
(`MainWindow::addCanvasActions`), appended to every branch so the three menus
cannot drift. The empty-canvas menu used to be Add and nothing else. The menu
opens during a run as well, showing only that block: it changes nothing, and
framing the field or reading the names is what a run is watched with.

**The canvas zoom is proportional.** `m_scaleStep` is read as a share of the
level, not a number of percent added to it: a flat ten took one notch from 100%
to 110% and the same notch from 500% to 510%, so coming back out from far in
took thirty of them. `PlotPanel`'s wheel always worked this way. Ctrl zooms
because every other editor zooms with it, and Shift still does because this one
always has; a bare wheel scrolls.

**A dropped file is handled by a method, not only by the event.** Qt delivers a
drop through its own drag manager, so a `QDropEvent` sent at the window with
`sendEvent` never reaches `dropEvent` -- a test cannot post one.
`MainWindow::openDroppedUrls` is where the decision and the open live, and
`dropEvent` is a two-line wrapper over it; what stays untested is Qt's own
delivery.

**A caption is the application's own action on the world**
(`Rule::showCaptionAction()`, text and shown in `actionParams`): rich text over
the canvas, under the log and above the controls, written in the editor behind
the `...` button on the rule card. Run state like everything else, so `stop()`
clears it. `MainWindow` stacks the three -- log, caption, controls -- as
widgets over the canvas rather than drawing them into the scene, so they keep
their size whatever the zoom and a control can actually be clicked.

**A caption can carry fields**, `{score}`, `{time}`, `{ball:speed}`,
`{power:value}`, and the grammar is `ValueText`'s: what a field is, how
it is spelt, and how a value is written for a person, which the log shares.
A bare name is a variable, then a world reading; `object:property` is
anything a rule can read, then a control by its id. The scene keeps the
caption as written, and `SimulationController::expandCaption` fills it in on
each step; the window redoes the box only when the words change. A field
nothing answers to is left as written, since a typo should show. The field
needs no space after the brace, because the editor's own HTML carries
`p, li { white-space: pre-wrap; }`. Both converters write a caption with
fields as a function the page or window asks every frame, with the same
reads their rules use.

**The grammar of that address lives in `SceneControl` and nowhere else.** The
cards show a property as one string, `Start:caption`; the separator, the
joining (`SceneControl::key`) and the taking apart (`SceneControl::parse`,
which accepts every separator the program has ever written) are that struct's
business. The *file* does not carry the punctuation at all -- it writes the two
things it is:

```json
"watch": { "control": "speed", "property": "value" }
```

That field is the same field a plain property goes in, because a rule object
carries a condition's `watch` and the first action's `property` side by side.
It was one flat string with a dot in it once, and changing the dot to a colon
left every saved scene naming nothing: a spelling the cards choose must not be
able to do that, which is what the two fields are for.

A few properties the *editor* also has to recognise, because it draws with
them: whatever the engine tags `PropertyRole::Sensor` (hatched rather than
filled), `PropertyRole::Density` (where a body balances), and the three that
say a joint is a spring -- `SpringEnabled`, `SpringStiffness`, `SpringRestLength`
-- which make it draw a coil rather than a shaft. It asks for the key by role
-- `CanvasScene::isSensorShape`, `shapeDensity`, `isSpringJoint` -- and still
names none of them.

**A spring's turns come from its rest length, not from a number somebody
picked.** The count is its rest length over Options -> Physics -> Coil pitch, so
a long spring has more of them than a short one; from then on the count is
fixed and the turns spread as it is pulled and pack together as it is squashed,
which is what a spring does. Nothing has to know the joint's travel limits --
most joints have none, `enableLimit` being off by default, and the length
already obeys them where they are on. `CanvasScene::springTurns` works the
count out only while the scene stands still, because that is when a joint is at
rest, and holds it through the run. Only the kinds drawn between two anchors
get a coil: a revolute's spring is rotational and a zigzag along its ring would
mean nothing. The exported page does the same, from a turn count the converter
writes into its drawing list -- which is why `SceneExporter` hands converters
each property's `role`, by name rather than by number.

**The log says what a value is; a plot says what it did.** A `CanvasScene::Watch`
carries `plotted` -- "Add to Log as Plot", or "Plot as Well" on one already
logged -- and `SimulationController::samplePlots` takes one sample of each
marked watch per *step*, not per frame: a frame is not a step, at x4 it is four
of them, and a scene that cannot keep up drops some. The samples belong to the
run and go when it stops. A series fills at 20000 points, and then every series
is halved and the stride doubled, so a run of any length keeps its whole shape
rather than stopping at an arbitrary minute.

**The pointer over a trace reads it.** A dashed line down every band -- they
share the clock, so one line serves all of them -- a dot where each trace was
at that moment, each caption reading that value instead of its latest, and the
moment itself over the clock. The line lands on the nearest *sample*, not on
the pixel under the mouse, so the number quoted is one that was actually
recorded. `saveImage` turns it off around the render: the pointer is over the
menu by then, and a crosshair frozen into a saved picture is a stray mark.

`PlotPanel` keeps nothing and only draws. One band per trace, each with its own
vertical axis: a force in newtons and a position in scene units share no axis
worth sharing. **That axis spans the whole trace, not the part on screen** --
scoped to the window it moved under every new sample, and a scale that shifts
as you watch is one you cannot read a value off. The time axis is the one they
do share and is drawn once, under the last band. It shows **five seconds and
scrolls with the run**, since a minute squeezed into a tab is a solid block:
the wheel widens or narrows that window, dragging walks back through the run,
and the right-click menu clears the traces, fits the whole run, or writes them
out as CSV (one shared time column, since every series is sampled on the same
step) or as a PNG.


**`pixelsPerMeter` converts, and does nothing else.** Scene units to metres on
the way into the engine, metres back to scene units on the way out. Gravity is
handed over as it stands -- 9.81 is 9.81 -- and so are the speed thresholds.

There was a `m_motionScale` of `50 / pixelsPerMeter` on gravity and five
thresholds, so that changing the scale left the motion on screen alone. It is
gone. It made the scale a clock as well as a ruler and made the "m/s^2" on the
gravity row true only at 50 px per metre. What it bought is real and is now
yours to manage: `pixelsPerMeter` says how big the drawing *is*, so changing it
on a finished scene re-declares every size, mass and distance in it. A 425 px
gap is 4.25 m at 100 and 0.425 m at 1000, and the second one falls in a third
of the time -- correctly. Pick the scale for the size of thing being built and
leave it: **100 is the default**, which makes a few hundred pixels a few
metres. At 1000 the same drawing is a tabletop, and a tabletop bounces like one.

The steps in the catalogues are sized for that default -- a square metre of the
default density weighs about 10 N, so a force steps by 1 and a spring frequency
by half a hertz, which is a spring you can see. They are static: the catalogue
is built without knowing the scene's scale, so a scene at a very different one
wants different numbers. Making them follow `pixelsPerMeter` needs the scale to
reach the engine before the catalogue is asked.

**A distance written for a person has one owner**, `Units` -- the unit a
ruler is numbered in, what a scene coordinate is worth in it, and how many
decimals that is worth writing. Scene units are what a drawing is drawn in and
`pixelsPerMeter` is the only thing that makes them a real distance, so the
conversion sits beside the rest of what that scale does rather than in the
widget that happens to need it first.

The rulers step in the unit being *shown*, not in scene units: the step is the
next 1, 2 or 5 times a power of ten that keeps the labels about 64 device
pixels apart, with minor ticks at fifths of it where they would not crowd. A
fixed stride could not work -- one scene unit is a different distance in every
unit and at every zoom, and the old flat 20 put a tick every two pixels at
10%. The unit rides on the zero mark rather than after every number, and the
origin is found by value: at a fine step it is written "0.0" or "0.000", so
matching the text left the unit off exactly where it belonged. Both rulers
carry the choice because they are two edges of one canvas -- each offers it on
its own context menu but neither keeps it; `MainWindow::applyRulerUnit` sets
both and the INI holds it under `Physics/rulerUnit`.

**The chrome is themed; the canvas is not.** `Theme::apply` puts Fusion, a
palette and one application stylesheet on everything (Options -> Common ->
Colour scheme, `Appearance/theme`; System follows the desktop where Qt can
tell -- the dialog says colour scheme and style, the code says theme and
controls, which is what the INI keeps).

**Neither of the two is written in C++.** The same bargain as the plugins, one
step further in: `Theme` knows how to read a definition and nothing about what
any of them say. They are files under `themes/`, read at startup -- one JSON per
colour scheme and per style, over one shared `base.qss` -- so adding either is
adding a file, and the format is documented for the user in `docs/themes.md`.
A folder of the user's own (`themes` beside their settings, or whatever
`PHYSALIS_THEMES` names) is read
after the installed one, so a file named for a scheme that ships redraws it
rather than appearing beside it. A file's *name* is its key, the way a
converter's folder is its id: nothing inside it can disagree about which scheme
it is.

A colour scheme is ten colours in `themes/schemes/<key>.json`; the palette, the
derived colours and the sheet are all worked out from them. The shape of the
controls is a second, independent choice (Style, `Appearance/controls`):
`themes/styles/<key>.json`, radii and paddings that fill the shared sheet's
`@r...` and `@pad...` placeholders, plus a few rules of its own laid over it,
and `include` for a run of rules two styles share rather than a copy in each. A
style that is only a different radius is a name in a list and nothing else, so
corners are the least of what separates them. Each sets its own **density** --
the paddings run from Sharp, at a third of Material's, up to Material itself --
its own way of filling a button (bordered, a Fluent gradient, a bold tonal
capsule, or only its words), its own field (bordered, bottom-stroked, filled
and underlined, or tinted until focus) and its own mark on the tab in hand (a
soft pill, a bar above, a bar below, or just the accent in the word).

**The tick, the radio, the slider and the scroll bar are each style's own
too**, and that is most of what tells them apart: left to the shared sheet
they were the same controls at another radius, which is what these all were
once. Material's slider is the 2024 one -- a tall pill of a track with a thin
upright handle standing in a gap of its own; Fluent's is the ringed WinUI
thumb on a thin rail; Sharp's is a square grip on a hairline, with its ticks
and scroll bars sized down to match; Minimal's is a dot on a line and a scroll
bar barely there.

**System is the one that is not a shape**, and so has no file, the way System
has no scheme file: the platform's own style with no sheet of ours over it. It wears the platform's palette too, and the panels' cards follow the
desktop's light or dark rather than the chosen theme -- a native style reads
little of a palette, so a theme laid over it would be half applied, light text
on a button still drawn light. An unknown key in the settings file falls back
to the first real shape instead, since that is a typo and not a request. The combo and spin
arrows and the checkbox tick are images under `:/theme`, one per ink. A widget's colour is a `palette(...)` role where one fits and a
`Theme::colors()` field where none does -- never a hex literal -- and a
"muted" label is `setProperty("muted", true)`, which the app sheet colours.
Whatever bakes a colour in -- a panel's own sheet, a painted pixmap -- redoes
it on `Theme::changed`. Icons go through `ThemedSvgEngine` in `Icons.cpp`,
which draws their greys lighter on dark at paint time, so a switch needs no
icon set again. The field, the grid and the physics view's colours are the
scene's and Options' and are left alone; the caption and controls standing
on the field keep `Theme::lightPalette()` for that reason.

**The side panel's tabs are icons.** Six names across a narrow panel left no
room for any of them, so each tab keeps its name in a tooltip and only the one
you are on spells it out (`MainWindow::updateSidePanelTabLabels`). **View** on
the menu bar is a checkbox per panel; unchecking one takes its tab out --
`QTabWidget` cannot hide a tab, so `applyVisibleTabs` removes them all and puts
back the ones wanted, in order -- and the choice is remembered under
`Panels/hidden`. It refuses to leave none.

**The field is the scene's, and Options holds what a new one starts with.**
Everything the `*.phys` carries in its `field` block -- size, background, grid
-- belongs to the scene and comes back with the file. `MainWindow` keeps the
defaults in `m_fieldDefaults` and `applyFieldDefaults()` puts them on a new
scene, on the empty scene at startup, and on the scene a file is about to be
read into, so a file that says nothing about its field does not inherit the
last one's. `applySettings` no longer touches the open scene's field, and
`currentSettingsSnapshot` reads the defaults rather than the scene -- which is
what used to carry one scene's background into the settings and from there
into every scene after it.

**One body can be kept in the middle of the view.** `PhysicsBody::isFocused()`
is the application's own flag -- no engine has an opinion about where a camera
points -- and `CanvasScene::setFocusedBody` is its only writer, which is what
keeps it to one body. Both views follow it on `SimulationController::stepped`,
and each gives *its own* `sceneRect` a rectangle wide enough to hold the body:
`centerOn` will not scroll past the scene's own, so a body that has left the
field would otherwise slide away with nothing following it. The field stays
what the scene says it is.

**A tool is a preview with Apply and Cancel, not a dialog.** The Tools list on
the toolbar -- Clone, Mirror -- and the conversions on a shape's context
menu open `ToolOptionsBar` in the bottom-left of the canvas, declare their
settings as a list of `ToolOption`, and re-run on every change:
`MainWindow::beginTool` holds the scene as it was, `runTool` undoes the last
preview and lays down a new one, and Cancel -- or a click outside the panel --
leaves nothing behind. A tool that destroys the shape it is converting goes
through `CanvasScene::destroyShape` rather than deleting it, since the scene
may still hold it as the active selection.

**A rule can be followed back to what it names.** Double-click an object box on
a rule card, or take it from its context menu, and `CanvasScene::revealObjectNamed`
selects whatever answers to that name -- shape, body, joint, ray or explosion --
and the Properties tab comes forward. Selecting any of those on the canvas or
in the Objects tree does the same.

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
    Rule.h            one rule: a list of conditions joined all-of or any-of,
                      and a list of actions carried out in order
    SceneVariable.h   a named value the scene carries and no engine knows
    SceneControl.h    a checkbox, button or slider the scene is worked with,
                      and the grammar of how a rule addresses one
    ValueText         how a value is written for a person, and a caption's
                      {fields}
    ControlsPanel     the Controls tab: one card per control
    RulesPanel        the rule cards
    VariablesPanel    the Variables tab beside them
    PropertyPanel + PropertyPane/*   property tables, one pane per selection kind
    PlotPanel         the Plot tab: what the log kept, drawn against the run
    SimulationController   drives the engine, polls events, applies rules
    SceneSerializer   *.phys read/write
    SceneExporter     finds export converters and runs one
    SceneScreenshot   the scene as drawn, cut to its objects, to an image, SVG or PDF
    MainWindow, UndoStack, OptionsDialog, AboutDialog, SceneTree
ui/               .ui forms (AUTOUIC searches here, not beside the sources)
resources/        icons (physalis.svg), the .rc that gives the exe its icon
themes/           how the chrome looks, read at startup and not compiled in:
                  schemes/*.json and styles/*.json over one base.qss; the
                  format is docs/themes.md, which is the user's page for it
engines/box2d/    one plugin: its own CMakeLists, include/ and src/
tests/            one .cpp per scenario, all built into a single binary
exporters/        export converters -- one folder each, all JavaScript
    box2d-qt-project/  manifest.json, export.js, templates/ -> one plain Box2D
                       program, main.cpp; rules become ifs after b2World_Step
                  It writes no code of its own but the rules': every
                  object -- world, body, shape, joint, helper, toolbar -- is a
                  template under templates/objects/, filled by render() in
                  export.js. A placeholder alone on a line takes a block; a line
                  whose value is null is left out, so a template lists every
                  field and a scene sets only what differs from the default; a
                  `//!` line is a note that is not written; a placeholder the
                  script does not fill is an error naming the template
    box2d3wasm/        the same bargain, one index.html on a canvas
                  Both carry what belongs to no engine as well as what does:
                  the log, the scene's own controls, the keys a rule watches
                  and the caption a rule writes. A rule the converter cannot
                  write is reported by name through io.log() and left out --
                  never written wrong, never dropped in silence
tools/            view_exports.py -- serves exported scenes and lists them on
                  one page; a wasm export needs a server, not a file:// URL
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

Parts can be left out with options, all `ON` by default: `WITH_BOX2D`
(the engine plugin), `WITH_EXPORTER_BOX2D_QT` (which converter
folders are installed -- they are JavaScript, nothing is compiled) and
`WITH_TESTS`. The
suite runs scenes on Box2D, so `WITH_TESTS` refuses to configure without it.

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
- **The two copy and paste tests wait for the clipboard rather than trusting
  it** (`tests/ClipboardProbe.h`). On Windows one process holds the clipboard
  open at a time, and while something else has it -- a clipboard manager,
  another Qt program -- every `setText` is dropped and read back empty while
  still reporting success. `theClipboardAnswers()` keeps asking for two
  seconds, which is longer than any real contention, and
  `SKIP_IF_THE_CLIPBOARD_STOPPED_ANSWERING()` sits at the copy itself because
  the clipboard can go away between the probe and the copy -- so red still
  means Copy is broken and only a clipboard that never comes back skips. A
  macro because `GTEST_SKIP` leaves the function it is written in. Both were
  skipping on a momentary blip, which is a hole where a passing test belongs.

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

**Copy and Duplicate carry the mechanism, not the shapes.**
`SceneSerializer::fragmentToJson` writes a piece of a scene the way the file
writes the whole of one -- the shapes picked, the bodies they belong to, and
the joints *between those bodies* -- and `fragmentFromJson` reads it back with
fresh names, re-pointing each joint at the copies rather than at what they were
copied from. Both go through the same `readBody`/`readJoint` the file does, so
a body pasted cannot drift from a body loaded. A joint with only one end in the
selection is dropped and counted for the status bar: attaching the copy to the
original's partner would be inventing a linkage nobody asked for. The Clone
tool still copies shapes alone -- its preview re-runs on every keystroke and
`undoTool` only tracks shapes, so bodies and joints it made would pile up.

**A sliding joint's rail belongs to whichever of its two bodies holds still.**
`CanvasScene::railOf` answers where the travel reads zero and which way the
limits run from there. Zero is the *far* anchor at rest, because the engine
measures travel as the gap between the anchors and adds that gap to both
limits (`travelOrigin` in `Box2DJoints.cpp`) -- hung off anchor A the rail came
out short of the real travel by the whole gap, which ran crane.phys's rails off
the end of the crane. And it is held, for the length of a run, in the frame of
the body that holds still rather than always body A: domino.phys's lifts are
built with body A the car and body B the guide, and pinned to A the limit line
rode up the screen with the car. Where neither body is static the rail is body
A's, which is the engine's own reference. The direction turns round with the
end, since the anchor running along the rail then travels the opposite way.

**Joints are coloured and drawn by kind, not by type.** A type id belongs to one
engine (`revolute` is Box2D's); the five `JointVisual`
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

**A trail is where a body has been**, and the same split as the slingshot:
whether a body draws is the body's (`PhysicsBody::drawsTrail()`, the "Draws a
Trail" row, `"trail": true` in the `*.phys` only when set) and how the line
looks -- colour, width, style -- is an application setting (Options -> Physics
-> Trail). Options holds six colours, `trailColors` in the INI (the single
`trailColor` before it is read as the first): `CanvasScene::trailColorFor`
gives each body that draws the next one, counted in the scene's body order so
a body keeps its colour from run to run, and past the sixth turns them a
golden angle further round the wheel, because several lines in one colour
cannot be told apart. A trail takes its colour index when it starts and keeps
it, so a body a rule starts mid-run takes the next one rather than shifting
everyone after it in the list.

**A rule works the pen through properties, not an action.** Whether a body
draws and its fade are flags like any other, so they are set, toggled and
watched like any other: `Rule::trailProperties()` puts them beside the
engine's body properties, the way `runProperties()` does for the world, and
`SimulationController` answers those two keys from the run's copy of the body
before anything is asked of the engine. A Draw trail action with an on/off
parameter came first and was a second way of writing a boolean. Wiping is
`clearTrailAction()`, since it is not a value. Lifting the pen is
`setDrawsTrail(false)` on the run's copy, so the scene's own flag is untouched. A trail is a list of strokes: lowered again, the next
point starts a new one, so a gap is a gap and not a line joined across it.
The converters carry no trails, so a rule with either action is reported and
left out of an export, as any rule they cannot write is.

**The fade is the body's** (`PhysicsBody::trailFadeMs`, the "Trail Fade (ms)"
row, `"trailFadeMs"` in the `*.phys` only when set; 0 keeps the whole line),
because two bodies in one scene want different tails -- it was an Option for
an hour. Every point carries the run time it was taken, and `setTrailClock`
drops what is older than each body's own fade every step. A fading line
changes every pixel every frame, so it skips the pixel cache and is drawn
afresh by `drawFadingTrails`, in 24 bands of one strength: drawn a segment at
a time, the round ends of translucent segments overlapped in a bead at every
joint. The cache resets the painter's transform to lay its pixels down, which
is why `drawTrails` saves round it -- the fading trails after it are drawn in
the scene's. The point drawn from is the centre of mass, worked out once in the
body's own frame and carried through its transform, and
`SimulationController::sampleTrails` takes one point per *step*, for the reason
the plots do: at x4 a frame is four steps, and a line through every fourth
point cuts the corners off a pendulum. The points are the scene's
(`extendTrail`, drawn in `drawForeground` over the shapes -- under them a slow
body hid nearly all of its own line) and are run state. What is already drawn
is kept as pixels covering the view and half a view round it, and a frame
strokes only the new points: stroking the lot every frame took a run to 10 fps
in two minutes. They are run state: there for the whole run, cleared by `stop()`
before the clones that drew some of them are deleted. A trail that fills is
halved and takes every other point from then on, the plots' bargain, and a body
at rest adds nothing. `SceneScreenshot::contentArea` takes in
`trailBounds()`, because a picture cut to the bodies alone loses most of the
trace. A body that draws is marked by four ticks round its centre of mass in the
trail's colour, in Physics mode only -- open in the middle and not round,
since anything round there was lost among the joint anchors -- painted apart from the body axes, which can be switched off and would
have taken the only sign of it with them. The exporters do not carry trails.

**Shapes lying over one another** are reached with Ctrl+click, in Edit and
Physics mode alike: it steps down through `CanvasScene::shapesAt` -- everything
under the pointer, top first -- from the current selection to the shape beneath,
and round to the top again. A plain press on the shape already selected keeps
it even where another lies on top, so a shape reached that way can still be
dragged or double-clicked into a body (Ctrl+double-click makes that body
static). The slingshot looks through shapes that
cannot be shot to the topmost one that can.

**A rule is a list of conditions and a list of actions.** `Rule` holds
`QVector<RuleCondition>` and `QVector<RuleAction>`, joined by one `Rule::Join`
-- all of these, or any of these -- for the whole card. One joiner rather than
brackets, drawn in every gap and kept in sync: `a and b or c` reads two ways
without them, and a card that can be written ambiguously is worse than one that
cannot. The rule fires as the *combination* becomes true, once, not per
condition; then every action runs in the order listed. An action that names the
other object and finds none is passed over on its own and the rest still happen.

A file writes the first condition and the first action flat on the rule, as it
always did, and puts the rest in `conditions` / `actions` arrays beside them.
A reader prefers the arrays where they are there, so a scene written before
either existed loads with no second path through the reader.

**An unfinished rule is kept, marked and passed over.** It used to be dropped
the moment the scene was saved, which took however much of it had been written.
`Rule::problem()` names the first blank in card order; the panel turns the card
red, marks the row it came from, and puts a count on the Rules tab. The file
carries `"incomplete": true`, worked out again from the fields on the way back
in rather than trusted. `SceneExporter` filters them out before handing the
document over: a converter turns each rule into code and has no business
deciding whether one is finished.

**Two comparisons look at the step before:** `changed to` and `changed from`.
No engine keeps a previous value, so `SimulationController` does -- one
snapshot of every watched reading taken before any rule fires, so each condition
on a step sees the same answer however far down the list it sits. Keyed by the
property rather than by the rule, since the value it had belongs to the
property. Neither fires on the first step: there is no step before it. They
compare exactly, which makes them worth using on readings that step between
settled values and wrong on a position, which can pass through a number without
landing on it.

**A removal can be answered.** Before a rule's action takes a body away -- an
engine marks such actions `ActionType::removesBody` -- `SimulationController`
looks for rules on that body (or its shapes) watching the application's own
event `Rule::aboutToBeRemovedEvent()`. If there are any, they are carried out
instead and the body stays; if not, it is removed. "The other object" in an
answer is the subject of the rule that did the removing. An answer that itself
removes the body really removes it: `m_handlingRemoval` stops it asking again.

**The world raises one event, the application's own:** `Rule::runStartedEvent()`,
"starting simulation", carried out once in `start()` with the world built and
the snapshot taken, before the first step -- so a rule can set things up.

**"Init state" is the application's own action on a body:** back where it stood
when the run started (taken in `start()`, before the start rules), facing the
same way, and not moving. The engine's position, angle and velocity properties
are found by role -- `PropertyRole::PositionX` and the rest.

**"Clone" is the application's own action on a body** (`Rule::cloneAction()`,
X and Y in `actionParams`): a copy of every shape, through the same JSON as
Duplicate, posed as the run's snapshot found the parent -- so a removed body
can still be cloned -- and moved so its origin lands on X, Y, then added to the
world. Clones are run state: `PhysicsBody::isRunOnly()`, made without
`bodiesChanged` (every panel listing bodies would rebuild per clone), and gone
with the run's copy of the scene. Rules name no clone.

**Read-only rows are there before a run too.** They read what the object
starts with: `SimulationController::initialValue` builds the scene into a
world it never steps (`buildWorld`, the same code `start()` uses), reads it, and
drops it at the end of that pass of the event loop so it never answers for a
scene that has since changed. A reading an engine marks `rulesOnly` -- bounds,
centre of mass, last hit, position and angle (edited on the canvas), a circle's
radius, a sensor's count -- stays out of the table and in the rule menus.

**A run is read, not edited, and the panels go read-only rather than away.**
Rules, Variables and Controls each answer `simulationRunningChanged` with a
`setReadOnly`, which disables the editors on their cards and leaves everything
else alone; coming back out of it rebuilds, because what each editor's enabled
state should be is decided while the card is built and putting the lot back on
would hand the first rule card a Move Up it never had. Two things deliberately
stay live: a rule card's object boxes, since double-clicking one is how a rule
is followed to what it names, and the Objects tree, which changes nothing and
is the only way to point the panels at another object mid-run -- a press on the
canvas pans the field. Both were written for and then made unreachable by a
`setEnabled(!running)` that greyed the whole panel out.

**A run never touches the scene.** `SimulationController::start` saves the
scene and loads it into a `CanvasScene` of its own (`runScene()`), and
everything a run does -- moving shapes, a rule writing a joint, a clone, a
trail, a caption, a control's value -- happens to that copy. The canvas and
the full-screen view show the copy while the run goes (`MainWindow::shownScene`),
and Stop deletes it: there is nothing to put back, so nothing can be put back
wrong. The scene itself only gets `setSimulationRunning`, which is what makes
the panels read-only. Whatever the window draws with -- colours, layers, zoom
-- reaches the copy through `setRunSceneSetup` and `applySceneLook`. A run's
copy is never drawn at rest, so `setSimulationRunning(true)` measures the
rails and spring turns that are meant to be taken at rest there and then.

**The undo stack records nothing while a run is going.** `UndoStack::push`
marks the stack touched and returns instead, which is what keeps Save offered
over a mid-run edit -- adding a row to the log is one, and is meant to happen
there.

**A property row may speak for several objects.** `rowsFromCatalogue` takes a
list of value maps, reads the first and writes all of them, which is how the
physics and sensor panes set a property on every shape picked -- and picking a
body picks all of its shapes. The pane adds a read-only count row so the table
says how many it is speaking for. A name is the one row that cannot be spread
across a selection, since a rule finds its subject by exactly one.

**Three world actions are the application's own**:
`Rule::stopRunAction()`, `holdRunAction()` and `restartRunAction()`, offered on
the world because that is what a rule names when it means the simulation
itself. No engine knows a run is being watched, let alone how to end one. They
cannot be carried out where a rule fires -- that is inside the step, with the
solver on the stack -- so `SimulationController` remembers one and acts on it
once the step is over. A restart throws the run's copy away and makes a new one
without ending the run in between (`endRun` then `start`), so the full-screen
view stays up. Sliders and checkboxes carry over, and a value carried away
from its start value is a change: the rules following that control run at once,
even in a run that opens held, or a variable copied from a slider would read
the start value while the slider showed another. The window does not
rebuild the control widgets for the new copy: they write to whichever copy is
shown, and one torn down under a press never gets its release.

## Exporting

**File -> Export to -> ...** is built from whatever converters are found in
`assetFolders("exporters")` -- installed, then the user's own, then
`PHYSALIS_EXPORTERS`, the same three the themes come from, later standing in for
earlier by folder name. There is no setting for the path: one that can be
pointed away from what ships is a setting whose only power is to hide it. A converter is a
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

**An export has to agree with the editor about more than the shapes**, and the
three it got wrong are worth stating:

- **Gravity goes over as it stands**, with the speed thresholds. Both
  converters carried the `m_motionScale` of `50 / pixelsPerMeter` for a while
  after the application dropped it, so every exported scene fell at half
  gravity. A converter working in a metre of its own -- box2d3wasm chooses one
  at which Box2D's fixed 2 cm is the scene's contact margin -- re-quotes them
  at that metre instead, which is the same motion in scene units.
- **A rule that watches an event fires on every step the event is raised**,
  the way `SimulationController` does it; only a rule made of readings alone
  waits for the value to cross. Written as a rising edge, a ray that pushes a
  ball for half a second pushed it once.
- **A clone's place is scene units**, handed to the one helper that turns a
  pair of them into metres. Converting each number first put the clone a
  hundredth of the way there.

`tests/exporters.cpp` is where these live, and the suite compares what a
converter writes against what the application hands the solver rather than
against itself.

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
from the same tree: the program, the engine plugins beside it, the export
converters under `exporters/` and the theme definitions under `themes/`.
Those last are not optional -- with none of them the program can only wear
the platform's own look. Build Release first -- a Debug executable is
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
- **A Linux package says which distribution it was built against** --
  `physalis_0.1.58_ubuntu24.04_amd64.deb` -- since that is what decides where it
  can be installed. The name comes from `/etc/os-release` at configure time.
  A symlink or anything else an `install(CODE)` writes has to go through
  `$ENV{DESTDIR}`: cpack installs into a staging folder, and a path built from
  `CMAKE_INSTALL_PREFIX` alone lands in the real `/usr/bin`, which is refused.
- **Linux** carries none of Qt: the distribution's own packages provide it.
  Which package is built is whichever the machine has the tools for --
  `dpkg-shlibdeps` gives a `.deb`, `rpmbuild` an `.rpm`, and a `TGZ` is always
  made; asking for the other one fails the whole run. Both read what the
  program and the plugins actually link and write the dependencies from the
  packages providing them (`CPACK_DEBIAN_PACKAGE_SHLIBDEPS`, rpm's own
  autoreq), so the list cannot drift from the build. The program
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

One number, `PHYSALIS_FULL_VERSION`, in the About box and on every package.
`project(Physalis VERSION ...)` says what a working copy is; a build that ships
is handed the tag it was built from instead, `-DPHYSALIS_VERSION=0.1.58`, which
has to be digits and dots or the configure fails. `Version.h` is written at
configure time from `cmake/Version.h.in` and included by `main.cpp` only, never
by `PhysalisCore`.

There was a build counter in a file in the build directory. It counted builds
of one folder, which is nothing anybody wants to read on a package: CI, whose
folder is new every run, called its packages 0.1.1 whatever the tag said.

## Style

Match the surrounding code. Comments explain *why*, in prose, and are worth
writing only where the reason is not on the line itself — several of the
"things that have bitten before" above are recorded as comments at the place
they matter. No decorative headers, no restating the code.

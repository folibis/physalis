Box2D / Qt project
==================

Turns a Physalis scene into a small, self-contained CMake project: one Qt
window that builds the scene in Box2D and runs it, rules and all.

What comes out
--------------

    CMakeLists.txt      Qt6 Widgets, and Box2D by FetchContent
    main.cpp            QApplication and the window
    Viewport.h/.cpp     steps the world, runs the rules and paints it
    Rules.h/.cpp        the rule runtime -- events, conditions, actions
    Scene.h             the shapes of the generated data
    Scene.cpp           GENERATED -- the scene itself

Only Scene.cpp is written from the scene, and even that is assembled out of
templates: templates/parts/ holds one for every kind of thing a scene can
contain -- a body, a shape of each kind, a joint of each type, a ray, an
explosion, a rule. export.js decides what goes in the holes and nothing else;
there is no C++ in it. To change what the exported code looks like, edit a
template.

Scale
-----

One pixel per scene unit, the same as the editor at 100%, so a scene comes out
the size it was drawn. The window is not stretched to fit the field: a field
larger than the window shows its middle, which is where a scene is. Crop to
objects narrows that to a box around what is actually there.

Settings
--------

Options -> Export -> Box2D / Qt project:

    Project name          the CMake project, the executable, the window title
    Add controls          a toolbar with Start, Pause and Stop
    Qt prefix path        written in as CMAKE_PREFIX_PATH, so the project
                          configures without being told where Qt is
    Steps per second      how often the exported project steps the world
    Debug view            adds a Debug view switch to that toolbar: body axes
                          and where the rays are looking, over the scene
    Crop to objects       show just what the scene contains, with a small
                          margin, rather than the whole editor field
    C++ standard          17, 20 or 23
    Box2D repository      where FetchContent pulls Box2D from -- a fork, a
                          mirror, a local clone
    Box2D tag or branch   which commit of it to build

Whatever the repository and ref name has to speak the Box2D v3 API: that is
what the generated code is written against, and nothing here checks it.

Building what comes out
-----------------------

    cmake -S . -B build
    cmake --build build
    build/<name>

It needs Qt 6 and a compiler; Box2D is fetched during configure, so the first
build wants a network connection.

Space runs and pauses, R goes back to the beginning, D turns the debug view on
and off -- as do the toolbar buttons, when the export asked for them.

Rules
-----

They are carried across as *data*, not as generated code: Scene.cpp fills in a
SceneRule per rule, and Rules.cpp reads them the way the editor does. So a
scene with forty rules is forty rows rather than forty branches, and the
behaviour lives in one file that can be read and fixed.

What the runtime covers:

  * events -- contact begin/end/hit, sensor enter/leave, joint limits,
    body moved and came to rest, and what a ray detects
  * conditions -- any readable property of a body, shape, joint, ray or the
    world itself, compared with = != < > <= >=
  * rising edges, so "while touching" fires once and not sixty times a second,
    and `once` rules that fire only ever once
  * values -- typed in, or read from another object's property plus an offset,
    which is how one thing follows another
  * the four operations: set, add, toggle, negate
  * "the shape that touched it" and "the body that touched it" as targets
  * actions -- explode, push at a point, remove a body, break a joint
  * rays, cast every step, and explosions as named points

Colours
-------

Shapes are painted the way the editor's physics view paints them: by what the
body is, not by the colours the shape was given while it was being drawn.
Dynamic, static and kinematic each get their own colour, filled at the
editor's transparency and outlined at full strength. Those are preferences
rather than anything in the scene, and arrive in scene.settings.Physics; with
no settings file to read, the shapes' own colours are used instead.

What is not carried
-------------------

Nothing of the editor: no selection, no undo, no property panel. The exported
project runs the scene, it does not edit it.

Units
-----

Physalis quotes lengths in scene units and divides by pixelsPerMeter on the
way into Box2D. Speeds and accelerations are quoted at a reference scale of
50 px/m and multiplied by 50/pixelsPerMeter, so that changing the scale does
not change how fast anything looks. Both conversions are done here, at export
time, and baked into the numbers in Scene.cpp -- so the exported project runs
at the same pace as the scene it came from. The rule runtime undoes them the
same way when a rule reads a speed back.

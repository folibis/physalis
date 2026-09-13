Box2D / Qt project
==================

Turns a Physalis scene into one plain Box2D program and the CMake file that
builds it.

What comes out
--------------

    CMakeLists.txt      Qt6 Widgets, and Box2D by FetchContent
    main.cpp            the whole program

main.cpp reads like the Box2D manual:

    createWorld()   b2CreateWorld, then every body, shape and joint, each id
                    kept in a variable named after the object in the editor
                    (body_1, circle_1, revolute_2 ...). A def field is written
                    only when it is not already Box2D's default.

    step(dt)        b2World_Step, then what the scene's rules say, as ordinary
                    if statements calling Box2D:

                        // lift up stop
                        bool liftUpStop = b2RevoluteJoint_GetAngle(revolute_2) < rad(-14);
                        if (liftUpStop && !liftUpStopBefore) {
                            b2Joint_WakeBodies(revolute_2);
                            b2RevoluteJoint_SetMotorSpeed(revolute_2, 0.0f);
                        }
                        liftUpStopBefore = liftUpStop;

                    A rule on an event -- two shapes starting to touch, a
                    sensor being entered -- is a loop over Box2D's own event
                    arrays. A rule on a value acts on the step the value first
                    crosses, which is what the bool beside it remembers.

    drawing         b2World_Draw with a b2DebugDraw whose callbacks paint with
                    QPainter. Each shape carries its colour in
                    shapeDef.material.customColor. The debug view draws joints
                    and body axes itself, the way the editor does: Box2D's own
                    joint drawing is sized for a world measured in whole metres.

Contact events: b2DefaultShapeDef leaves them off, so a shape that an event
rule watches gets shapeDef.enableContactEvents = true -- the editor switches it
on for those shapes when a run starts, and without it the rule never fires.

    SceneView       a QWidget: a timer that calls step() and repaints.

Nothing of the editor comes along: no scene description, no rules as data.

Units
-----

Box2D works in metres. Lengths are written m(...) in the units the scene was
drawn in, angles rad(...) in degrees; m() and rad() are two lines at the top of
the file. World speeds -- gravity, thresholds -- are quoted in the editor at a
reference of 50 px/m and come out already converted.

Settings
--------

Options -> Export -> Box2D / Qt project:

    Project name          the CMake project, the executable, the window title
    Add controls          a toolbar with Start, Pause and Reset
    Qt prefix path        written in as CMAKE_PREFIX_PATH
    Steps per second      how often the program steps the world
    Debug view            starts with joints and centres of mass shown, and a
                          toolbar switch for them
    C++ standard          17, 20 or 23
    Box2D repository       where FetchContent pulls Box2D from
    Box2D tag or branch   which commit of it to build (v3 API)

Building it
-----------

    cmake -S . -B build
    cmake --build build

Space runs and pauses, R goes back to the beginning, D shows joints and centres
of mass.

What is not carried
-------------------

A rule Box2D has nothing for stays in step() as a comment saying so, and the
export says which in its message at the end. A joint that holds a body to a
point in the world (a mouse joint) is not exported.

---
title: Physics engines
nav_order: 7
---

# Physics engines

Physalis is an editor and a player. It does not calculate the physics itself.
The calculation is done by a **physics engine**, a separate library loaded as
a plugin. The engine decides how bodies fall, collide, bounce and slide, and
how joints hold them together. Physalis draws the result and lets you build and
control the scene.

Physalis comes with plugins for two well-known open-source engines:

- **[Box2D](https://box2d.org)**, the default. It is fast and accurate, and is
  used in a great many games. Its joints are Revolute, Distance, Weld,
  Prismatic, Wheel, Motor, Mouse and Filter.
- **[Chipmunk2D](https://chipmunk-physics.net)**, a lightweight engine with a
  wider choice of constraints: Pivot, Pin, Slide, Groove, Damped Spring, Rotary
  Spring, Rotary Limit, Ratchet, Gear, Simple Motor and Mouse.

**Help → About** lists the engine plugins Physalis found.

## What an engine decides

The editor asks the engine what it can do and builds itself from the answer,
so the following all come from the engine:

- the properties on the **Body**, **Shape** and **Collision** pages, and the
  settings of the world;
- the joint types and their settings;
- the events a [rule](rules.md) can wait for, and the actions it can perform.

The common properties have the same names in both engines: velocity, density,
friction, restitution, damping, sensors and collision groups. The features
Physalis adds on top, such as the slingshot, the log, **to be removed**,
**starting simulation**, **Init state** and **Clone**, work in the same way
whichever engine a scene uses.

## Which engine a scene uses

A scene is built for one engine and keeps it:

- A **new** scene uses the engine chosen in **Options → Common → Physics
  engine**.
- The engine is saved in the scene's file, and the scene always opens with it.
- If the engine a scene needs is not installed, Physalis does not open the
  scene and tells you which engine it needs.
- Changing the engine in Options while a scene is open offers to save that
  scene, then starts a new, empty scene with the new engine.

An existing scene cannot be moved to another engine, because the two offer
different joints and partly different properties, and part of the scene would
be lost.

## Scale

Engines work in real units: metres, kilograms and seconds. The field's
**Pixels per Meter** setting decides how the canvas maps onto them. At the
common setting of 1000, a 40×40 box is 4 cm across and weighs a couple of
grams. At that scale, forces of a few tenths of a newton already move things
a lot. If a scene behaves too strongly or too weakly, check the scale first.

## How engine plugins work

This section is for developers who want to understand the plugin system or add
support for another physics engine.

### What a plugin is

An engine plugin is a shared library (`.dll` on Windows, `.so` on Linux,
`.dylib` on macOS) whose file name starts with `physalis_`, for example
`physalis_box2d.dll`. It must sit in the same folder as the Physalis program.

At startup Physalis looks at every library in that folder whose name starts
with `physalis_`, loads it, and calls a function named `physalisEngineApi`. A
library counts as a plugin only if that function exists and returns the exact
text `PhysalisEngine-1`. Any other library is ignored. This protects Physalis
from unrelated libraries and from plugins built for a different version of the
interface.

A plugin exports four plain C functions:

- `physalisEngineApi()` returns `"PhysalisEngine-1"`;
- `physalisEngineName()` returns the name shown to the user, such as `"Box2D"`;
- `physalisEngineVersion()` returns the version of the engine inside, such as
  `"3.1.1"`. It is optional, and appears in **Help → About**;
- `physalisCreateEngine()` creates and returns a new engine object.

You do not write these by hand. The header `include/PluginApi.h` provides a
macro that writes all four; put it in exactly one source file of the plugin:

```cpp
#include "PluginApi.h"
#include "MyEngine.h"

PHYSALIS_DECLARE_ENGINE("My Engine", "1.0", MyEngine)
```

The engine object is a C++ class derived from `physics::IPhysicsEngine`
(`include/IPhysicsEngine.h`). Because the interface passes Qt types such as
`QString` and `QVariant`, a plugin must be built with the same compiler and the
same Qt version as Physalis itself. The simplest way to do that is to build it
as part of the Physalis source tree, next to `engines/box2d` and
`engines/chipmunk`, which also serve as complete examples.

### The engine describes itself

Physalis contains no knowledge of any particular engine. Instead, it asks the
engine to describe itself, and builds the property tables, the joint menu and
the rule cards from the answers. An engine answers these questions:

- `bodyProperties()`, `shapeProperties()` and `worldProperties()`: the
  properties of a body, a shape and the world;
- `jointTypes()`: the joint types it offers, with their settings, and
  `jointReadables(type)`: what a joint of that type can report while running;
- `bodyEvents()` and `shapeEvents()`, plus the events listed in each joint
  type: what a rule can wait for;
- `bodyActions()` and `jointActions()`: what a rule can do.

These questions are asked often, and also when no simulation is running, so
they must be answered without a world existing.

Each property is described by a `physics::JointParam` (`include/JointTypes.h`).
Its fields decide how Physalis treats it:

- `key` is the property's permanent name. Scene files store values under it,
  and it is how the engine recognises the property later, so it must never
  change. `label`, `section` (the page it appears on) and `tooltip` are what
  the user sees.
- `type` (a number, a whole number, an on/off switch or a list of choices),
  `defaultValue`, `minValue`, `maxValue`, `decimals` and `step` shape the input
  field.
- `stored` means the value belongs to the object. Physalis shows it as an
  editable row, saves it in the scene file when it differs from the default,
  and hands it to the engine when the world is built. Density and friction are
  examples.
- `liveReadable` means the value can be read while the simulation runs, such
  as speed or contacts. Readable values that are not stored appear as
  greyed-out readings, and can be logged and used in rule conditions.
- `liveSettable` means a rule may change the value while the simulation runs.
- `rulesOnly` keeps a readable value out of the property table while leaving
  it available to rules. `mirrorsSetting` marks a reading that only repeats a
  setting already shown.
- `role` marks the few properties Physalis itself needs to recognise: the
  sensor switch (sensors are drawn hatched), density (for the centre of mass),
  the impulse used by the slingshot, and the position, angle and velocity used
  by **Init state**. Physalis finds these by role, never by name.

Events (`EventType`) have an `id`, a `label` and a `description`, and
`namesOther` says whether the event involves a second object, such as the
shape that was touched. Actions (`ActionType`) have their own parameters, and
`removesBody` marks an action that takes a body out of the world, so that
Physalis can offer the **to be removed** answer first.

### A simulation, step by step

When the user presses Simulate, Physalis drives the engine as follows:

1. **`createWorld(world)`** creates an empty world. `WorldDesc` carries the
   scale (`pixelsPerMeter`) and the world's stored properties, such as gravity.
2. **`addBody(body)`** is called once for each enabled body. It returns a
   handle, a whole number that Physalis uses to refer to that body from then
   on. `BodyDesc` carries the body's type, position and rotation, its stored
   properties, and its shapes (`ShapePart`). Each shape has a name, its
   geometry (box, circle, polygon or chain, relative to the body), and its own
   stored properties. A shape marked `watchedByRules` is watched by a rule, and
   the engine must switch on whatever it needs to report that shape's
   contacts. Returning an invalid handle refuses the body, and Physalis tells
   the user it was skipped.
3. **`addJoint(joint)`** is called once for each joint, with the handles of
   its bodies, its anchors, its axis and its settings.
4. Then, for every frame:
   - **`step(dt)`** advances the world by a fixed time step. Faster playback
     calls it more often; it never passes a larger step.
   - **`bodyState(handle)`** is asked for every body: where it is, how it is
     rotated, whether it is awake, and whether it still exists. Physalis moves
     the shapes on the canvas to match.
   - **`pollEvents()`** returns what happened during the step, such as contacts
     beginning and ending, sensors being entered or joints reaching their
     limits. Each `EngineEvent` names the body, the shapes involved and the
     event's `id`. Physalis matches them against the rules.
   - **`castRay(from, to, mask)`** is asked for every ray.
   - Rules read values with `bodyValue`, `shapeValue`, `jointValue` and
     `worldValue`, change them with `setBodyParam`, `setShapeParam`,
     `setJointParam` and `setWorldParam`, and perform actions with
     `performAction`, `performJointAction` and `performActionAt` (actions aimed
     at a point, such as an explosion).
   - **`takeProblems()`** returns anything that went wrong during the step, as
     sentences for the user.
5. **`destroyWorld()`** is called when the user presses Stop. Physalis then
   puts the scene back as it was, so the engine keeps nothing between runs.

Physalis also builds a world that it never steps, to show an object's starting
values, such as its mass, before a simulation begins.

### Units

The engine receives and returns everything in the units the editor uses:
positions, sizes and distances in field units (pixels, with Y pointing down),
and angles in degrees. Converting to the engine's own units, usually metres
and radians, is the plugin's job, using `pixelsPerMeter` from the `WorldDesc`.
Values must come back in the same units they were entered in.

### Rules a plugin must follow

- **Never end the program.** A scene may ask for something impossible, and
  some engines stop the whole process when their internal checks fail. A
  plugin must catch such failures, take the affected body out of the
  simulation, and report it through `takeProblems()`.
- **Keep keys stable.** A property's `key`, a joint type's `id`, and event and
  action ids are written into users' scene files. Renaming one means old scenes
  lose that value.
- **Describe only what really works.** Whatever the plugin lists appears in
  the interface, and users will try it.

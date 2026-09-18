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

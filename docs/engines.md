---
title: Physics engines
nav_order: 7
---

# Physics engines

Physalis does not simulate anything by itself. The physics comes from an
**engine plugin**, and Physalis ships with two:

| Engine | Good to know |
|---|---|
| **Box2D** | The default. Joints: Revolute, Distance, Weld, Prismatic, Wheel, Motor, Mouse, Filter |
| **Chipmunk2D** | Joints: Pivot, Pin, Slide, Groove, Damped Spring, Rotary Spring, Rotary Limit, Ratchet, Gear, Simple Motor, Mouse |

**Help → About** lists the engines that were found.

## Which engine a scene uses

- A **new** scene uses the engine chosen in **Options → Common → Physics engine**.
- The engine is saved in the `*.phys` file, and the scene always opens with it.
- A scene whose engine is not installed is not opened; Physalis says which
  engine it needs.
- Changing the engine in Options while a scene is open offers to save that
  scene, then starts a new, empty scene with the new engine.

A scene cannot be switched to another engine, because engines differ in their
joints and in part of their properties.

## What differs between engines

- **Joint types**: each engine has its own. See [Joints](joints.md).
- **Properties**: the Body, Shape and Collision pages show only what the
  engine offers.
- **Events and actions**: the rule cards list what the engine reports and can do.

Most everyday settings have the same name in both engines: position, velocity,
density, friction, restitution, sensors and collision groups. The
application's own features, such as the slingshot, **to be removed**,
**starting simulation** and **Init state**, work the same way in both.

## Scale

**Pixels per Meter** (in the field's properties) sets how many canvas units
make one metre. At the common 1000 pixels per metre, bodies are light and
forces are small: tenths of a newton already move things.

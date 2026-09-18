---
title: Home
nav_order: 1
---

# Physalis

Physalis is a desktop application for building and running two-dimensional
physics simulations. A scene is made of **shapes**: rectangles, circles and
polygons. Shapes are grouped into **rigid bodies**, and each body has its own
physical properties: mass, friction, bounciness, damping and the way it moves.
Bodies can be connected by **joints**, such as hinges, sliders, springs, ropes
and motors. During a simulation you can push them with **forces and
impulses**, measure distances with **rays**, set off **explosions**, and
describe how the scene should react to events with **rules**, for example
*when the ball enters the pocket, return it to the start*.

![The Physalis window: a scene in Physics mode, with a body selected and its properties on the right](images/overview.png)

## How the simulation works

Physalis does not calculate the physics itself. The calculation is done by a
**physics engine** that is loaded as a plugin. Physalis includes plugins for two
widely used open-source engines, [Box2D](https://box2d.org) and
[Chipmunk2D](https://chipmunk-physics.net). Everything the editor offers,
including the properties you can set, the joint types, and the events and
actions available to rules, comes from the engine the scene uses. See
[Physics engines](engines.md).

## The window

**The canvas** fills most of the window. It is the field on which the scene is
built. Rulers along the top and left edges show the coordinates: **X** grows to
the right and **Y** grows downwards, as is usual for screen graphics. A grid
helps with lining shapes up. Drag an empty part of the canvas to move around
the field, and use the zoom box on the toolbar (or **Shift + mouse wheel**) to
zoom in and out.

**The toolbar** holds the file commands, the tools for the current mode, the
simulation controls and the zoom. On its right is the **Edit / Physics**
switch, which chooses the mode you are working in. The toolbar only shows the
tools that make sense in that mode.

**The side panel** on the right has three tabs:

- **Properties** is where you inspect and change whatever is selected. Its
  contents follow the selection: a shape shows its size, position and
  appearance; a body shows its type and physical properties; a joint shows its
  settings. With nothing selected, it shows the scene itself: the size and
  colour of the field, the grid, the scale, and the settings of the physical
  world such as gravity. It is described in detail [below](#the-properties-table).
- **Objects** lists everything in the scene as a tree, grouped into bodies
  (with the shapes each one is made of), joints, explosions, rays, and shapes
  that are not yet part of a body. Clicking an entry selects that object on the
  canvas, which is the easiest way to reach something small, hidden under
  another shape or outside the visible area. Right-click the tree to expand or
  collapse its groups.
- **Rules** holds the rules that run during a simulation. See [Rules](rules.md).

**The status bar** at the bottom always describes the current selection and
what can be done with it next. When you are unsure what to do, look there.

## Two modes

The work is divided into two modes, switched with the buttons on the right of
the toolbar.

- **Edit mode** is for drawing. You add shapes, move, resize and rotate them,
  edit the points of polygons, and choose colours and outlines. Nothing here is
  physics yet: a shape on its own takes no part in a simulation. See
  [Drawing shapes](editing.md).
- **Physics mode** is for making the drawing behave. You group shapes into
  bodies, decide whether each body moves or stays fixed, set what it is made
  of, connect bodies with joints, add rays and explosions, and run the
  simulation. See [Bodies and physics](physics.md).

## The properties table

The Properties tab is a table with the name of each property on the left and
its value on the right. Longer lists are divided into pages. A body, for
example, has **Body**, **Shape** and **Collision** pages.

- **Change a value** by typing into it, using its arrows, ticking its box or
  choosing from its list. The change applies straight away and can be undone.
- **A name in bold** means the value differs from the default. A small
  **reset arrow** appears beside it; click it to go back to the default.
- **Greyed-out values are readings**, not settings. Examples are a body's mass
  (worked out from its shapes and their density), its speed, and the number of
  contacts it has. Before a simulation they show what the object starts with.
  During a simulation they show the live value and update as it runs.
- **Hover over a name** to see an explanation of the property.
- **Right-click a name** and choose **Add to Log** to watch that value during a
  simulation. Logged values are listed in the top-left corner of the canvas
  while the simulation runs. See [Watching values](running.md#watching-values).

## A scene from start to finish

1. In **Edit** mode, draw the shapes: a floor, a box and a ball.
2. Switch to **Physics** mode. Double-click each shape to make it a body. Hold
   **Ctrl** while double-clicking the floor to make it a **static** body, one
   that never moves.
3. Select the ball and give it some bounciness (**Restitution**) on its
   **Shape** page.
4. Optionally connect bodies with [joints](joints.md), or add [rules](rules.md)
   for anything that should happen during the run.
5. Press **Simulate** (▶). The box and the ball fall onto the floor. See
   [Running a simulation](running.md).
6. Press **Stop** (■). Every body returns to exactly where it was, so you can
   adjust the scene and run it again.

## Saving and sharing your work

- **Save and open scenes** from the **File** menu. A scene is stored in a single
  `.phys` file, which holds everything: shapes, bodies, joints, rules and
  settings. A star (`*`) in the window title means there are changes that have
  not been saved yet.
- **Undo and redo** any change with **Ctrl+Z** and **Ctrl+Y**. Stopping a
  simulation is not an undo step, because stopping already puts everything back.
- **Copy and paste** shapes with **Ctrl+C** and **Ctrl+V**, including between
  two scenes.
- **Export** a scene as a standalone program that runs it without Physalis:
  a C++ project or a web page. See [Exporting](exporting.md).

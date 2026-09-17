---
title: Running a simulation
nav_order: 6
---

# Running a simulation

The run controls are on the toolbar in **Physics** mode.

![The toolbar in Physics mode](images/toolbar-physics.png)

| Control | What it does |
|---|---|
| **Simulate** (▶) | Starts the run. While it runs, the same button **pauses** it |
| **Step** | Advances one frame and holds |
| **Stop** (■) | Ends the run and puts every body back where it started |
| **×1** | Playback speed, from ×¼ to ×8 |
| **View** | What the canvas shows while the run goes |
| **Full Screen** | Runs the simulation on a screen of its own |

Nothing can be edited during a run. Dragging on the canvas pans the field.

## Speed

The speed box changes how fast the run plays: ×2 covers two seconds of the
simulation in one second of yours. The physics is the same at every speed; only
the pace you watch it at changes.

## What a run shows

The **View** list switches parts of the picture on and off while a run goes:

- **Grid**
- **Joints**
- **Body axes**
- **Rays**
- **Explosions**
- **Sleep shading**: tints each body by whether it is still moving

Click an entry to switch it; the list stays open so you can set several.
These only affect a run. While editing, everything is always drawn.

## Full screen

With **Full Screen** ticked, pressing Simulate opens the run on a screen of its
own, with the field as large as it fits.

| Key or mouse | Effect |
|---|---|
| **Space** | Hold the run, and let it go again |
| **→** | Advance one frame |
| **Esc** | End the run and go back to the editor |
| Drag | Pan |
| Mouse wheel | Zoom |
| **0** | Fit the whole field again |

## The slingshot

A dynamic body with **Can Be Shot** ticked can be flung with the mouse during
a run, like a pool cue:

1. Press on the body.
2. Pull back. A line runs from the body to the mouse; its colour goes from the
   light-pull colour to the full-pull colour as the pull gets stronger.
3. Let go. The body is pushed the other way, harder the further you pulled.

**Esc** or a right-click while pulling cancels the shot.

![Aiming a shot](images/slingshot.png)

- **Max Power** (on the body) is the push at full pull.
- **Max Pull** (on the body) is how far to pull for full power. Pulling
  further adds nothing.
- The line's colours, width and style are in
  [Options → Physics → Slingshot](options.md).

A body that can be shot has a dotted outline just inside its border, so you
can tell which one it is. A shot reaches through shapes that cannot be shot:
a ball under a table can still be shot.

## When something goes wrong

A scene can ask for the impossible, such as a motor thousands of times stronger
than what it drives. Instead of crashing, the engine takes the runaway body out
of the run and says what happened in the status bar. Press **Stop** to put the
scene back.

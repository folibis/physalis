---
title: Joints
nav_order: 4
---

# Joints

A joint connects two bodies: a hinge, a slider, a spring, a rope.

![A revolute pendulum, a prismatic slider with its travel, and a distance joint](images/joints.png)

## Adding a joint

1. In **Physics** mode, select the two bodies.
2. Press **Add Joint** and choose the type.
3. Drag the joint's anchors (the rings) to where it should attach.

The first body you picked is **body A** and the second is **body B**. For
most joints the order does not matter; for the Wheel joint it does (see below).

To remove a joint, select it and press **Delete Joint**.

## Joint types

The types depend on the scene's [physics engine](engines.md).

### Box2D

| Type | What it does |
|---|---|
| **Revolute** | Pins two bodies at a point and lets them rotate about it. A hinge, a wheel on an axle |
| **Distance** | Holds two points a fixed distance apart. With a spring it is a shock absorber, with a limit a rope |
| **Weld** | Fixes two bodies together. A frequency makes the join slightly soft |
| **Prismatic** | Lets two bodies slide along one direction and nothing else |
| **Wheel** | Lets one body spin while sliding along an axis of another: a wheel on a suspension arm |
| **Motor** | Drives body B to hold a set position and angle relative to body A |
| **Mouse** | Pulls a point on a body softly towards a point in the world |
| **Filter** | Holds nothing; only stops the two bodies colliding with each other |

### Chipmunk2D

| Type | What it does |
|---|---|
| **Pivot** | Pins two bodies at a point and lets them turn about it |
| **Pin** | Holds two points a fixed distance apart, like a rod hinged at both ends |
| **Slide** | Keeps two points between a shortest and a longest distance. With no shortest, a rope |
| **Groove** | A slot in the first body and a pin on the second that runs along it |
| **Damped Spring** | A spring with a shock absorber between two points |
| **Rotary Spring** | A torsion spring that turns the second body back towards an angle |
| **Rotary Limit** | Stops the second body turning past a range of angles |
| **Ratchet** | Lets the second body turn one way, and catches it if it turns back |
| **Gear** | Makes the two bodies turn together at a ratio |
| **Simple Motor** | Turns the second body at a steady rate |
| **Mouse** | Pulls a point on a body towards a point in the world |

## Limits, motors and springs

Select a joint and its settings appear in the **Properties** tab, grouped into
pages such as **Spring**, **Limit** and **Motor**.

### Sliding joints measure from where they start

For **Prismatic** and **Wheel** joints, the limits are travel measured from
where the joint is when the run starts, along its axis. The band drawn along
the axis shows that range, starting at **anchor B**.

- A range of **0 to 120** lets body B slide up to 120 units along the axis.
- If body A is the one that moves, sliding towards body B counts as
  *negative* travel. Swap the bodies, or use a negative range.

### Motors

A motor with a very large **Max Motor Force (N)** can push straight through its
limits. Bodies at the usual scale are light, and tenths of a newton are
already strong. Start small and increase.

A rule with **Negate** on **Motor Speed** reverses a motor, for example when
it reaches a limit. See [Rules](rules.md).

### Wheel joints: chassis first

For a **Wheel** joint, **body A must be the chassis** and **body B the wheel**.
The suspension axis belongs to body A. With the wheel as body A, the axis turns
with the wheel and the suspension points the wrong way.

The spring's **Frequency (Hz)** sets how stiff the suspension is; doubling it
makes it four times stiffer. **Damping Ratio** sets how quickly it stops
bouncing: 0.7 is a good start, and 1 stops without bouncing.

## Bodies Collide

By default two bodies joined by a joint do not collide with each other. If
one of them is scenery, the other passes straight through it. Tick **Bodies
Collide** on the joint when they should still collide.

## How joints are drawn

Joints are coloured by kind: **Turning**, **Holding a length**, **Sliding**,
**Fixed** and **Anchorless**. Each kind's colour and line style, and the
**Anchor opacity**, are set in [Options → Joints](options.md).

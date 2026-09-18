---
title: Rules
nav_order: 5
---

# Rules

Rules make a scene react while it runs. Each rule says **when** something
should happen and **what** should then be done, for example:

- *when* the ball enters the pocket, *then* remove the ball;
- *when* the cart's slider reaches its limit, *then* reverse its motor;
- *when* 5 seconds have passed, *then* set off the explosion.

Rules are kept in the **Rules** tab on the right. Press **+** to add one. A
new rule appears as a card, which you fill in from top to bottom.

![Three rules: a pocket, a cue ball, and a timed speed-up](images/rules-panel.png)

## The rule card

The top line of a card holds its controls. The checkbox switches the rule on
or off without deleting it, which is handy while trying things out. The
triangle folds the card up to save space, and the bin deletes it. Double-click
the rule's name to rename it. The up and down arrows change the order; rules
are carried out in the order they are listed.

Below that, the card has three parts: **When**, **Then** and **Do**.

## When: the condition

First choose the object the rule watches: a shape, a body, a joint, a ray,
or **World** for the simulation as a whole. Then choose what about it to
watch. This can be one of two things.

### A property compared with a value

For example, *ball · Speed · is less than · 5*, or *World · Elapsed Time ·
is at least · 10*. The comparisons are:

- **equals** and **differs from**,
- **is greater than** and **is less than**,
- **is at least** and **is at most**,
- **is a multiple of**. The value is rounded to a whole number, and the
  condition holds at every non-zero multiple: with 5, at 5, 10, 15 and so on.
  *World · Frame · is a multiple of · 60* fires once every 60 steps, which is
  once a second at the usual rate.

For on/off properties the choice is simply **is** or **is not**, true or false.

A rule fires at the moment its condition **becomes** true, not over and over
while it stays true. *Speed is less than 5* fires once when the ball slows
down, and fires again only if it speeds up and slows down once more.

### An event

An event is something that happens at a particular moment. Which events exist
depends on the object and on the [physics engine](engines.md):

- **begins contact** and **ends contact**: another shape starts or stops
  touching this one.
- **is hit**: something strikes this shape harder than the world's hit
  threshold.
- **is entered** and **is left**: something moves into or out of a
  [sensor](physics.md#sensors).
- **is about to touch**: two shapes are about to collide.
- **starts moving** and **comes to rest**: a body wakes up, or settles and
  falls asleep.
- **detects**: a [ray](physics.md#rays) sees a particular shape.
- **Lower limit reached** and **Upper limit reached**: a joint gets to the end
  of its range.
- **to be removed**: a rule is about to remove this body. See
  [Answering a removal](#answering-a-removal-to-be-removed).
- **starting simulation** (on **World**): happens once, at the start. See
  [Setting things up](#setting-things-up-starting-simulation).

Events that involve a second object, such as *begins contact*, have one more
box: which object it has to be. Choose *anything*, or name one in particular.

## Then: the target

Choose what the rule acts on:

- any object in the scene, by name;
- **the shape that touched it** or **the body that touched it**, which means
  the second object from the event. One rule on a pocket can remove whichever
  ball fell in;
- **World**, to change the simulation itself.

## Do: the change or the action

Choose either a property to change or an action to perform.

### Changing a property

Choose the property, then the operation:

- **Set to** gives the property a new value.
- **Add** adds to its current value; a negative number subtracts.
- **Negate** reverses its sign. This is the usual way to send a motor back
  the other way.
- **Toggle** switches an on/off property to the opposite state.

The value can be typed in directly, or read from another object while the
simulation runs: choose **Property** instead of **Value**, then name the
object, its property and an offset to add. This is how one object can follow
another, for example a platform that always stays 50 units below a ball.

### Performing an action

Actions do something rather than set a value. The ones that come with every
engine are:

- **Remove** takes a body out of the simulation for the rest of the run.
- **Init state** puts a body back where it stood when the simulation started,
  facing the same way and not moving.
- **Clone** makes a new body identical to this one, with all its body and
  shape properties, at the **X** and **Y** you give. It works even on a body
  that has already been removed. Clones exist only while the simulation runs
  and are never saved.
- **Explode** sets off an [explosion](physics.md#explosions).
- **Break** destroys a joint.
- **Stop the simulation** (on **World**) ends the run and puts the scene
  back, just like the Stop button.
- **Hold the simulation** (on **World**) pauses the run where it is.

Engines add their own actions, such as a push or a force at a point on a body,
or working out a body's mass again after its shapes have changed.

## Answering a removal: "to be removed"

Before a rule removes a body, Physalis checks whether that body has a rule of
its own with the event **to be removed**.

- If it has **none**, the body is removed as usual.
- If it **has** one, the removal is cancelled and that rule is carried out
  instead. The body stays in the simulation.

```mermaid
flowchart LR
    A[A rule says: Remove the ball] --> B{Does the ball have a<br/>'to be removed' rule?}
    B -- no --> C[The ball is removed]
    B -- yes --> D[That rule runs instead<br/>and the ball stays]
```

This lets general rules treat one object differently without writing a
separate rule for each case. If the answering rule itself removes the body,
the body really is removed.

**Example: a pool table.** Each pocket is a static sensor with one rule:
*pocket* **is entered** by *anything* → **the body that touched it** →
**Remove**. The cue ball has one rule of its own: *cue ball* **to be removed**
→ *cue ball* → **Init state**. Every other ball disappears into the pockets,
while the cue ball returns to where it started. Seven rules cover all six
pockets.

## Setting things up: "starting simulation"

A rule on **World** with the event **starting simulation** runs once, when
you press Simulate and before anything moves. Use it to prepare the scene,
for example to give a body a starting speed, place an object, or open a gate.
**Stop** puts back everything these rules changed, as it does for every other
rule.

## Tips

- Give meaningful names to the objects your rules use. The rule cards show
  names, and `ball` is easier to recognise than `circle_7`.
- To see why a rule does or does not fire, add the watched property to the log
  (right-click it in the Properties tab and choose **Add to Log**) and watch it
  during the run.
- Untick a rule to switch it off for a moment instead of deleting it.

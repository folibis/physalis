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

A rule names objects, and those names are the way back to them: **double-click**
any object box on a card -- or right-click it and choose **Show** -- to select
that object and bring up its properties. It works for whatever the rule names:
a shape, a body, a joint, a ray or an explosion.

## More than one condition, more than one action

The **+** beside the **When** row adds another condition, and a small **and**
box appears between the two. Change it to **or** and the rule fires when any
one of them is true instead of when all of them are; it is one choice for the
whole card, so every gap shows the same word. Each condition after the first
has a button to take it away again.

The **+** beside the **Then** row does the same for actions: another **Then**
and **Do** pair, carried out in the order they are listed when the rule fires.
That is how one condition can set a property, perform an action and stop the
run without being written out three times.

A rule fires as the whole card *becomes* true, not once per condition -- so two
conditions joined with **and** fire once, when the second of them comes true.

Bear in mind that an event -- a touch, a sensor entered -- happens on a single
step and is gone. Joining a reading to an event with **and** is the useful case
("when the ball touches the ground *and* it is moving faster than 5"); joining
two events with **and** asks for both on the same step, which is rarely what
was meant.

The exporters write several readings and several actions without trouble. Two
events in one rule, or an event joined to a reading with **or**, have no single
place to live in the generated code, and the export says so in its report
rather than writing half the rule.

## Unfinished rules

A rule that still has a blank in it cannot run, and the card says so: it turns
red and a warning mark appears beside the name, with the missing piece named in
its tooltip. The Rules tab carries the same mark and a count, so an unfinished
rule is visible while you are working somewhere else.

An unfinished rule is kept like any other. It is saved with the scene and is
there again when the scene is reopened, so a rule half written at the end of one
session can be finished in the next. It is simply passed over while the scene
runs, and it is left out of an export.

## Variables

The **Variables** tab, next to Rules, holds values the scene carries that no
physics engine knows about: a score, a count of lives, a flag saying which way
a lift is going. Each has a name, a type -- **Boolean**, **Integer**,
**Double** or **Timer** -- and the value it starts every run at.

Rules reach them through a single object called **Variables**: pick it as the
thing watched or the thing acted on, and the variable itself is the property.
So *when Variables.score is greater than 100, stop the simulation* is written
the same way as any other rule.

A variable is run state. It starts each run at the value in the tab and goes
back there when the run stops, the way every shape goes back to where it
started.

### Timers

A **Timer** is a variable measured in milliseconds. It is read like any other
number -- *when Variables.clock is greater than 3000, open the gate* -- but it
is the one type that does not simply hold what it is given: while it is running
it counts up on its own, a step at a time.

A timer **stands still until a rule starts it**. That is deliberate: it is what
lets a timer measure the gap after something happens rather than time from the
beginning of the run. To time from the start of the run, start it on the
*starting simulation* event.

Beside the usual **Set to**, the action's list offers four things only a timer
has:

| Verb | What it does |
| --- | --- |
| **Start** | Begins counting, on from wherever it stands. |
| **Pause** | Stops counting and leaves the value where it is. |
| **Stop** | Stops counting and puts the value back to the one in the tab. |
| **Reset** | Puts the value back without stopping it, so it climbs again at once. |

*Reset* is what a timer measuring the gap between repeated events wants: wound
back on each one and still counting.

Compare a timer with **is greater than** rather than **changed to**. It advances
by the length of a simulation step -- about 17 ms at the usual rate -- so it
steps straight over most exact values, and a condition waiting for one of them
would never fire.

**Add to Log** on a variable's right-click menu shows it in the readout pinned
to the canvas, so you can watch it count while the scene runs. The entry says
**Remove from Log** once it is there.

Double-click a name to rename it, the way a rule card's title is renamed.
Renaming carries every rule and log row that named it along with it. **Remove
Variable**, also on the right-click menu, leaves the rules that used it marked
unfinished, so nothing goes wrong quietly.

## Keys and controls

Two things in the condition's first box belong to no engine and to no object in
the scene: **Keyboard** and **Controls**. They are how somebody plays a scene
rather than only watches it.

### Keys

Choose **Keyboard**, then **key pressed**, **key held** or **key released**,
then the key.
Any letter, digit or named key can be used -- the arrows, Enter, Space, Tab,
Backspace, Delete -- and **Ctrl**, **Shift** and **Alt** can be ticked beside
it, so *Ctrl+R* is a different key from *R*.

No key is reserved. A **press** and a **release** happen once each -- holding
the key down does not fire them again. **Key held** is the other way round: it
fires on every step the key is down, which is how a rule keeps doing something
for as long as it is held. A thruster is exactly that: press and there is a
force, let go and there is none.

Because it fires every step rather than once, the amount is per step: at 60
steps a second, an impulse of 1 a step is 60 a second.

For the keys to reach the scene, the canvas has to have the focus -- click it
once after pressing Simulate. In [full screen](running.md#full-screen) every
key but Esc goes to the scene.

**Example: a thruster.** *Keyboard* **key held** *Left* -> *the ship* ->
**Push at a Point**, with **Impulse Y** -1 and **Offset X** 80: while Left is
down the ship is pushed, 60 times a second, 80 units to one side of its centre
of mass, so it both lifts and turns. The offset is what puts the push on a
side; a push with no offset goes through the centre of mass and only moves it.

**Example: a flipper.** *Keyboard* **key pressed** *Left* → *left flipper
joint* → **Motor speed** **Set to** -720, and *Keyboard* **key released**
*Left* → the same joint → **Motor speed** **Set to** 240. The flipper snaps up
while the key is held and falls back when it is let go.

### Controls

A scene can carry its own controls. They are declared on the **Controls** tab
of the side panel, one card each:

- **Id** is how a rule addresses the control. Changing it carries every rule
  that names it along.
- **Caption** is what it says on the canvas. Leave it empty and it says its id.
- **Kind** is **Checkbox**, **Button** or **Slider**.
- A checkbox declares whether it **starts ticked**; a slider declares where it
  **starts**, its **lowest** and **highest** values and its **step**. A button
  declares nothing -- what it has is a press.

The controls appear over the canvas while the simulation runs, under the
caption, and they are gone when it stops. What a control is set to is run
state: it starts each run where the scene declares and goes back there when the
run ends.

Rules reach them through **Controls**:

| | |
|---|---|
| **value** | what it is set to: ticked or not, held or not, where the slider sits |
| **enabled** | whether it can be worked at all; a rule can grey one out |
| **caption** | what it says, which a rule can change mid-run |

and through three events: **pressed**, **released** and **changed**. Each names
which control it means, or *any control*. A button raises a press and a release
rather than a click, so a rule can drive something for exactly as long as the
button is held.

**Example: a motor with a speed slider.** A slider `speed` from 0 to 720, and a
button `start`:

- *Controls* **changed** *speed* → *the motor joint* → **Motor speed** **Set
  to** the **Property** *Controls · speed:value*;
- *Controls* **pressed** *start* → *the motor joint* → **Enable motor**
  **Toggle**;
- *the motor joint* **Motor enabled** **is** *true* → *Controls* →
  *start:caption* **Set to** `Stop`, and the same the other way round for
  `Start`, so the button says what pressing it will do.

Writing a control's value from a rule moves the widget on the canvas, and
pulling the widget is read by the rules -- a slider follows the arrow keys if a
rule says it should.

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
- **key pressed**, **key held** and **key released** (on **Keyboard**):
  somebody worked the keyboard. See [Keys and controls](#keys-and-controls).
- **pressed**, **released** and **changed** (on **Controls**): somebody worked
  one of the scene's own controls. See [Keys and controls](#keys-and-controls).

Events that involve a second object, such as *begins contact*, have one more
box: which object it has to be. Choose *anything*, or name one in particular.

A rule that watches an event is carried out on **every** step the event is
raised, not only on the first one. A ray that sees something for half a second
fires its rule thirty times, which is what makes *while the ray sees the ball,
push it* work. A rule made only of value comparisons behaves the other way: it
fires on the step its combination *becomes* true and then waits for it to stop
being true before it can fire again.

### Watching a value change

Two of the comparisons look at the step before as well:

- **changed to** fires on the step the reading *became* the value.
- **changed from** fires on the step it *stopped being* the value.

Both need the reading to have actually moved, so a value already sitting on the
target goes on being ignored, and neither fires on the first step of a run --
there is no step before it for anything to have changed from.

They compare exactly, which makes them worth using on readings that step
between settled values -- awake or asleep, a body type, how many things are in
a sensor, whether a ray is hitting anything. A position or a speed can pass
straight through a number between one step and the next without ever landing on
it, so **is greater than** is usually what is wanted there instead.

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
- **Increment by** counts it on from the value it already has.
- **Decrement by** counts it back down.
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
- **Keep in view** hands the view to this body: from then on it stays in the
  middle of the screen and the scene moves around it. Untick **Follow** to let
  it go again. One body at a time, and when the run ends the view goes back to
  whatever the scene itself keeps in view. See
  [Keeping one body in view](running.md#keeping-one-body-in-view).
- **Explode** sets off an [explosion](physics.md#explosions).
- **Break** destroys a joint.
- **Stop the simulation** (on **World**) ends the run and puts the scene
  back, just like the Stop button.
- **Hold the simulation** (on **World**) pauses the run where it is.
- **Show a caption** (on **World**) puts words on the canvas, under the log.
  Press the **...** button to write them: the editor there sets bold, italic
  and underline, a typeface, a size and a colour. Untick **shown** to take the
  caption down again. A caption is run state like everything else:
  stopping the run clears it.

Engines add their own actions, such as a push or a force at a point on a body,
or working out a body's mass again after its shapes have changed.

### Counting up and down

**Increment by** and **Decrement by** move the value the property already has
rather than replacing it, so a rule can count: a score going up by one each
time something is scored, a fuel level going down while a motor runs. Paired
with **changed to** on the thing being counted, that is a tally.

(**Increment by** is what used to be called **Add**; it is the same operation
under a name that says what it does. Files written either way still load.)

### A random value

Beside the value box is a die. Press it and the box becomes two, with **to**
between them: the rule then takes a fresh number somewhere between the two
every time it fires, rather than the one number you typed. A ball launched at
a random speed, a motor wound to a random angle, a score going up by a random
amount.

It works wherever a number is typed: the value an action writes, and every
parameter of an action -- where a **Clone** lands, how hard a **Push** pushes,
how wide an **Explode** reaches. The two ends are read in the same units as the
single value they replace.

Beside them is **by**, which decides what the rolled number is a whole number
of. Left at **any** it can be anything between the two ends; at `1` it rolls
whole numbers, at `10` it rolls tens, and `1 to 6 by 1` is a die with each face
as likely as the others.

### Only some of the time: "Chance"

The world has a reading called **Chance (0-100)**, and it is a different number
every time a rule looks at it. Comparing it is how a rule happens only
sometimes:

- `Chance` **less than** `25` is about one time in four.
- `Chance` **less than** `100` is every time; `less than 0` is never.

Put it beside an event with **all of these** and the event counts only some of
the time: *when the ball hits the wall, and Chance is less than 20, clone it.*

### Making a run repeat: the seed

Random would make a scene different every run, which is awkward when you are
trying to see whether a change helped. The field's **Random Seed** (in the
world's properties, during a run's mode) decides:

- **0** -- a different run every time.
- **anything else** -- the same run every time, exactly.

The seed travels with the scene and is used by the exports as well, so a scene
with chance in it still behaves the same way in an exported page or project as
it does here.

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
  during the run. A variable goes in the log from the Variables tab the same
  way.
- Untick a rule to switch it off for a moment instead of deleting it.
- A red card means the rule is unfinished, not that it is wrong: hover the
  warning mark to see which field is still empty.

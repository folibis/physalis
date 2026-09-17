---
title: Options
nav_order: 9
---

# Options

**File → Options…** holds the application's settings. They are saved and apply
to every scene.

## Common

![Options, Common tab](images/options-common.png)

- **Physics engine**: which [engine](engines.md) new scenes use.
- **Field**: the field's width, height and background colour, and the zoom range.
- **Grid**: whether grid cells are shown, their size and colour.
- **Snap**: whether shapes snap to the grid when moved, which point snaps,
  the snap step, and how close you have to get before it snaps.
- **History**: how many undo steps are kept.
- **Export**: the converters folder.

## Shapes

![Options, Shapes tab](images/options-shapes.png)

The default border and fill for new shapes, how the selection is drawn, and
the size and colour of the resize handles.

## Physics

![Options, Physics tab](images/options-physics.png)

- **Body Colors**: the colours of dynamic, static and kinematic bodies, shapes
  not in a body, and sensor hatching.
- **Engine Limits**: the most points a solid polygon can have, and the
  simulation rate.
- **Shape Drawing**: border width, fill opacity, and how strongly sleep
  shading tints resting bodies.
- **Body Axes**: the centre-of-mass cross: whether it is shown, its colours,
  length and width.
- **Selection**: how selected bodies are outlined.
- **Slingshot**: the pull line's colour for a light pull and for a full pull,
  its width and its style. See [the slingshot](running.md#the-slingshot).

## Joints

![Options, Joints tab](images/options-joints.png)

- **Joint Drawing**: default and outline colour, anchor radius, axis marker
  length, waist width, outline width, **Fill opacity**, and **Anchor opacity**
  (how see-through anchors are, so the shapes under them stay visible).
- **Joint Type Colors**: a colour and a line style for each kind of joint:
  Turning, Holding a length, Sliding, Fixed and Anchorless.
- **Joint Selection**: how a selected joint is outlined.

## Export

One page for each converter found, with that converter's settings. See
[Exporting](exporting.md).
